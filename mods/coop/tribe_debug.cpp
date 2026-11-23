#include "tribe.h"
#include "tribe_internal.h"
#include "tribe_logic.h"
#include "call_guard.h"
#include "lifecycle.h"
#include "mirror_registry.h"
#include "player_life.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "civ.h"
#include "coop_ui.h"
#include "creations.h"
#include "driver.h"
#include "effects.h"
#include "game.h"
#include "modes.h"
#include "npc.h"
#include "savestrip.h"
#include "space_ext.h"
#include "stage_epoch.h"
#include "world.h"
#include "owner_claims.h"
#include "autosave_glue.h"
#include "store.h"
#include "wsync.h"
#include "wire/tribe_anim.h"
#include "net/held_cmds.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/snapshot_rx.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "settings.h"
#include "fields.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {

// ---- debug (tests): orders through the same hooked functions the UI uses ----
static bool act_needs_object(int action) { return action == 3 || action == 12 || action == 15 || action == 24; }
static void* local_of(uint16_t id)
{
    if (C.sess && C.sess->role() == net::Role::Host)
        return hm_obj(id);
    return member_mirror(id); // (BUG-0955)
}
// BUG-0171 (0.7.11 N1): the dead-victim stop of the test commands' hits as a lab switch; off: blows on a victim at
// hp 0 or dead go through TakeDamage as before ba9dc46 (coop_tribe_epic_dying.py)
static bool g_fix_hit_dead = true;
COOP_FIX("tribe.hit_dead", "BUG-0171", g_fix_hit_dead, "tribe: the test commands' blows stop at a dead victim (no TakeDamage on hp 0)");
std::string debug(const std::vector<std::string>& a)
{
    // tests of the co-op mechanics through the game's damage path (host): ids as in coop.tribe
    auto hit = [](void* victim, void* attacker, float dmg) {
        float before = F<float>(victim, 0x5E0);
        // a dead victim takes no more blows: the test loop went on hitting the dying epic (BUG-0171)
        if (g_fix_hit_dead && (before <= 0 || game::is_dead(victim)))
            return std::string("hp 0.00 -> 0.00 (dead)");
        // the combatant's TakeDamage (vt 0x18): the same path as a real blow, with our hooks
        // (the push vector: (0,0,0) as the game's own bites and strikes, docs/re/creature.md 3)
        static const float zero[3] = {0, 0, 0}; // the hit direction must not be null
        vcall<int>((uint8_t*)victim + 0x5A8, 0x18 / 4, dmg, 0xFFFFFFFFu, 0, zero,
                   attacker ? (uint8_t*)attacker + 0x5A8 : nullptr);
        char b[64];
        snprintf(b, sizeof b, "hp %.2f -> %.2f", before, F<float>(victim, 0x5E0));
        return std::string(b);
    };
    if (a.size() >= 3 && a[0] == "fix") { // fix chiefs|gameover 0|1: the code before BUG-0081 / BUG-0172 (tests)
        bool on = atoi(a[2].c_str()) != 0;
        if (a[1] == "chiefs")
            g_fix_chiefs = on;
        else if (a[1] == "gameover")
            g_fix_gameover = on;
        else if (a[1] == "ko")
            g_fix_ko = on;
        else if (a[1] == "epic_players")
            g_fix_epic_players = on;
        else if (a[1] == "epic_herd")
            g_fix_epic_herd = on;
        else if (a[1] == "away")
            g_fix_away = on;
        else
            return "usage: coop.tribe fix chiefs|gameover|ko|epic_players|epic_herd|away 0|1";
        return "fix " + a[1] + " " + (on ? "on" : "off");
    }
    if (a.size() >= 2 && a[0] == "ring") { // ring <id>: the HUD shows the target ring under member / NPC creature <id>
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        char b[96];
        snprintf(b, sizeof b, "ring=%d drawn=%u order=%p", (int)ring_shown(c), g_rings_drawn, g_ring.target);
        return c ? std::string(b) : std::string("no such member");
    }
    if (!a.empty() && a[0] == "advance") {
        // tests (BUG-0576): the goal bar full (cPlayer +0x10F0 = +0x10F4), then the message 0x5CC2589 the stage's
        // listener 0xCD81F0 takes after a goal: with the bar full it shows the HUD's advance button (window 0x6454B47);
        // the test clicks it as a player (TRG2CVG_PreEditor, then the City Hall editor, docs/re/tribe.md#tribe-civ-transition)
        uintptr_t nmg = H->sym("Simulator::cGameNounManager::Get"), get = H->sym("App::IMessageManager::Get");
        void* nm = nmg ? skg::cdecl_call<void*>(nmg) : nullptr;
        void* p = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? F<void*>(nm, 0x74) : nullptr;
        void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
        if (game::mode_id() != modes::kTribe || !p || !skg::readable((uint8_t*)p + 0x10F0, 8) || !mm)
            return "not possible here";
        float& done = *(float*)((uint8_t*)p + 0x10F0);
        float need = F<float>(p, 0x10F4);
        if (need < 1)
            need = 1, *(float*)((uint8_t*)p + 0x10F4) = need;
        done = need;
        skg::vcall<void>(mm, 0x18 / 4, 0x5CC2589u, (void*)nullptr, (void*)nullptr, 0); // MessagePost
        char b[96];
        snprintf(b, sizeof b, "goal %.0f / %.0f, goal message posted", done, need);
        return b;
    }
    if (a.size() >= 2 && a[0] == "drop_chief") { // drop_chief <owner>: the chief leaves the tribe at once
        // (RemoveMember(c, destroy) as guest_state does for a mirror the host no longer sends):
        // BUG-0081, the next chiefs_tick must not call into it
        auto it = g_chief.find((uint8_t)atoi(a[1].c_str()));
        if (it == g_chief.end() || !it->second)
            return "no chief";
        void* c = it->second;
        uint16_t id = member_id(c);
        remove_member(c);
        return "dropped chief #" + std::to_string(id);
    }
    if (a.size() >= 2 && a[0] == "drop_member" && is_host()) { // drop_member <id>: tests (BUG-1053), the member leaves its
        // tribe at once (RemoveMember(c, destroy), as a fall or a raid's end); the host's table keeps it until the next tick
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c || !game::listed(c, 0x18EB4B7u))
            return "no such member";
        remove_member(c);
        char b[96];
        snprintf(b, sizeof b, "dropped member #%s %p listed_now=%d", a[1].c_str(), c, (int)game::listed(c, 0x18EB4B7u));
        return b;
    }
    if (a.size() >= 5 && a[0] == "guestcmd" && is_host()) { // guestcmd <from> <action> <target member id> <member id>:
        // tests (BUG-1053), a guest's order (MSG_TRIBE_CMD CMD_ACTION on a member as its target) handled here now, as
        // on_message does; then the ordered member's order record in the game
        uint16_t tid = (uint16_t)atoi(a[3].c_str()), mid = (uint16_t)atoi(a[4].c_str());
        void* tc = hm_obj(tid);
        net::Writer w;
        w.u8(CMD_ACTION);
        w.u8(1);
        w.u16(mid);
        w.u32((uint32_t)atoi(a[2].c_str()));
        Target t;
        t.kind = 1;
        t.id = tid;
        write_target(w, t);
        w.f32(0); w.f32(0); w.f32(0);
        uint32_t d0 = g_cmds_done, r0 = g_cmds_rejected;
        net::Reader r(w.data.data(), w.data.size());
        host_command((uint8_t)atoi(a[1].c_str()), r);
        void* mc = hm_obj(mid);
        char b[480];
        snprintf(b, sizeof b, "guestcmd: target #%u %p in_table=%d listed=%d ran=%u refused=%u | %s | member: ", tid, tc,
                 (int)(tc != nullptr), (int)(tc && game::listed(tc, 0x18EB4B7u)), g_cmds_done - d0, g_cmds_rejected - r0,
                 g_last_cmd.c_str());
        return std::string(b) + (mc && game::listed(mc, 0x18EB4B7u) ? ai_line(mc) : std::string("gone"));
    }
    if (!a.empty() && (a[0] == "graduation" || a[0] == "gradcine")) // BUG-1325 (tribe_graduation.cpp)
        return graduation_cmd(a);
    if (a.size() >= 4 && a[0] == "caption_race") { // see caption_race (BUG-1101)
        g_race_msg = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        g_race_post = a[2] == "post";
        g_race_before = a[3] == "before";
        return "armed " + a[1] + " " + a[2] + " " + a[3];
    }
    if (a.size() >= 2 && a[0] == "cine") { // cine <name> (tests, BUG-1101): this game plays that cinematic, outside the frame
        static std::string name;
        name = a[1];
        H->run_outside_frame(
            +[](void*) {
                uintptr_t get = H->sym("Simulator::cCinematicManager::Get"),
                          play = H->sym("Simulator::cCinematicManager::PlayCinematic");
                void* cm = get ? skg::cdecl_call<void*>(get) : nullptr;
                if (cm && play)
                    thiscall<int>(play, cm, name.c_str(), 1, 1, 0, 0, 0);
                H->log(M, SK_INFO, "tribe: tests: played the cinematic %s (now %08X)", name.c_str(), world::cinematic_id());
            },
            nullptr);
        return "playing " + name;
    }
    if (a.size() >= 2 && a[0] == "stale_chief") { // stale_chief <id>: as if a former chief's freed block
        // now held this member (another tribe's citizen): the next chiefs_tick must leave it alone
        // (BUG-0081; before the fix it got the old chief's size, 2.0 here)
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        g_chief_scale[c] = 2.f;
        return "stale chief entry for #" + a[1];
    }
    if (a.size() >= 2 && a[0] == "scale") { // scale <id>: the member's size in the game (spatial GetScale)
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        char b[64];
        snprintf(b, sizeof b, "scale %.3f", c ? game::scale(c) : 0.f);
        return c ? std::string(b) : std::string("no such member");
    }
    if (a.size() >= 4 && a[0] == "pincer" && is_host()) { // pincer <victim id>[,<victim id>...] <attacker id> <dmg>
        // (BUG-1083: several victims get their blows in this one call, i.e. in one frame, before any behaviour tick)
        void* at = local_of((uint16_t)atoi(a[2].c_str()));
        std::vector<void*> vs;
        for (size_t p = 0; p < a[1].size();) {
            size_t q = a[1].find(',', p);
            if (q == std::string::npos)
                q = a[1].size();
            void* v = local_of((uint16_t)atoi(a[1].substr(p, q - p).c_str()));
            if (!v)
                return "no such member";
            vs.push_back(v);
            p = q + 1;
        }
        if (vs.empty() || !at)
            return "no such member";
        std::string r;
        for (void* v : vs)
            r += (r.empty() ? "" : "; ") + hit(v, at, (float)atof(a[3].c_str()));
        return r;
    }
    if (!a.empty() && a[0] == "scales") { // the player tribe's members: size in the game / own size (BUG-0067)
        void* pt = game::mode_id() == modes::kTribe ? player_tribe() : nullptr;
        int n = 0, big = 0;
        float mx = 0;
        std::string list;
        for (void* c : pt ? members(pt) : std::vector<void*>{}) {
            float own = vcall<float>(c, 0x80 / 4, 0); // GetDesiredScale(int)
            if (own <= 0.01f || own > 10.f)
                continue;
            float r = game::scale(c) / own;
            n++;
            big += r > 1.15f;
            mx = std::max(mx, r);
            char e[48];
            snprintf(e, sizeof e, " %u:%.2f/%.2f", (unsigned)member_id(c), game::scale(c), own);
            list += e; // id:scale in the game/own size (tests, BUG-0152)
        }
        char b[96];
        snprintf(b, sizeof b, "scales members=%d big=%d max_ratio=%.2f", n, big, mx);
        return b + list;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "herds") { // the game's epic herds and epic creatures (BUG-0121)
        struct Cnt {
            uintptr_t is_epic;
            int herds = 0, ours = 0, enabled = 0, epics = 0;
        } n{H->sym("SporeCoop::cCreatureAnimal_IsEpic")};
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* n = (Cnt*)u;
                if (!game::object_alive(o))
                    return true;
                if (noun == game::kHerdNoun && skg::readable(o, 0x21C) && F<uint32_t>(o, 0x88) == kEpicArchetype) {
                    n->herds++;
                    n->ours += is_epic_herd_of_ours(o);
                    n->enabled += F<uint8_t>(o, 0x218) != 0;
                } else if (noun == 0x18EB45E && n->is_epic && !game::is_dead(o) && thiscall<bool>(n->is_epic, o))
                    n->epics++;
                return true;
            },
            &n);
        char b[192];
        snprintf(b, sizeof b, "epic herds=%d quiet_ours=%d enabled=%d live_epics=%d tracked=%p resets=%u world_changes=%u epoch=%u",
                 n.herds, n.ours, n.enabled, n.epics, g_epic, g_resets, g_world_checks_fired, game::stage_epoch(modes::kTribe));
        return b;
    }
    if (!a.empty() && a[0] == "airprobe") { // BUG-1082/1088 (diagnostics): members off their ground, every live epic
        // airprobe [min m]: one line per member of any tribe more than <min> m (default 1) over the terrain: id, owner,
        // height, node, locomotion mode +0x2B0, grounded byte +0x137, velocity, container +0x34, the nearest epic's distance
        float mn = a.size() > 1 ? (float)atof(a[1].c_str()) : 1.f;
        std::vector<void*> eps;
        uintptr_t is_epic = H->sym("SporeCoop::cCreatureAnimal_IsEpic");
        struct EU {
            uintptr_t is_epic;
            std::vector<void*>* v;
        } eu{is_epic, &eps};
        game::for_each_object(
            [](void* o, uint32_t noun, void* up) {
                auto* u = (EU*)up;
                if (noun == 0x18EB45E && game::object_alive(o) && !game::is_dead(o) && u->is_epic && thiscall<bool>(u->is_epic, o))
                    u->v->push_back(o);
                return true;
            },
            &eu);
        std::string out;
        char b[480];
        snprintf(b, sizeof b, "t=%.3f water=%.2f\n", H->time_seconds(), game::water_radius());
        out += b;
        for (void* e : eps) {
            net::Vec3 p = game::position(e), v = game::velocity(e);
            snprintf(b, sizeof b, "E %p pos=(%.2f %.2f %.2f) agl=%.2f lm=%d gr=%d v=(%.2f %.2f %.2f) scale=%.2f hp=%.0f ours=%d\n", e, p.x,
                     p.y, p.z, drive::over_ground(p), F<int>(e, 0x2B0), (int)F<uint8_t>(e, 0x137), v.x, v.y, v.z, game::scale(e),
                     game::health(e), (int)(e == g_epic));
            out += b;
        }
        for (void* t : all_tribes())
            for (void* c : members(t)) {
                net::Vec3 p = game::position(c);
                float h = drive::over_ground(p);
                if (!(h > mn))
                    continue;
                HEntry* it = hm_of(c);
                uint16_t id = it ? it->id : 0;
                net::Vec3 v = game::velocity(c);
                float de = 1e9f;
                void* ne = nullptr;
                for (void* e : eps) {
                    float d = net::dist(game::position(e), p);
                    if (d < de)
                        de = d, ne = e;
                }
                float sr = game::surface_radius(p); // BUG-1104: the height over the game's own surface point too
                snprintf(b, sizeof b, "M #%u %p owner=%d agl=%.2f surf=%.2f pos=(%.2f %.2f %.2f) %s lm=%d gr=%d v=(%.2f %.2f %.2f) cont=%p "
                         "epic=%p d_epic=%.2f anim=%08X\n", id, c, owner_of(c) == 0xFF ? -1 : (int)owner_of(c), h,
                         sr > 1.f ? drive::speed(p) - sr : -999.f, p.x, p.y, p.z,
                         member_brief(c).c_str(), F<int>(c, 0x2B0), (int)F<uint8_t>(c, 0x137), v.x, v.y, v.z, F<void*>(c, 0x34), ne,
                         ne ? de : -1.f, cur_anim(c));
                out += b;
            }
        // BUG-1104: the bodies the host still tracks (out of their tribe's list, dead, BUG-0620): every one, any height
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            HMember& hm = e_.data;
            if (!hm.body_since || !kept_alive(c, e_.ref.serial, 0x18EB4B7u))
                return;
            net::Vec3 p = game::position(c);
            float sr = game::surface_radius(p);
            snprintf(b, sizeof b, "B #%u %p owner=%d agl=%.2f surf=%.2f pos=(%.2f %.2f %.2f) %s lm=%d gr=%d body_age=%.1f anim=%08X\n",
                     e_.id, c, (int)hm.owner, drive::over_ground(p), sr > 1.f ? drive::speed(p) - sr : -999.f, p.x, p.y, p.z,
                     member_brief(c).c_str(), F<int>(c, 0x2B0), (int)F<uint8_t>(c, 0x137), H->time_seconds() - hm.body_since,
                     cur_anim(c));
            out += b;
        });
        return out + "end";
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "stats") { // BUG-0515: every live epic, its herd's tuning, its blows
        struct U {
            uintptr_t is_epic;
            std::string s;
        } u{H->sym("SporeCoop::cCreatureAnimal_IsEpic"), {}};
        game::for_each_object(
            [](void* o, uint32_t noun, void* up) {
                auto* u = (U*)up;
                if (noun != 0x18EB45E || !game::object_alive(o) || game::is_dead(o) || !u->is_epic || !thiscall<bool>(u->is_epic, o))
                    return true;
                void* h = game::herd_of(o);
                bool hr = h && skg::readable(h, 0x21C);
                auto& d = g_epic_blows[o];
                char b[360];
                snprintf(b, sizeof b,
                         "epic %p ours=%d hp=%.0f/%.0f scale=%.2f herd=%p arch=%08X gen=%d scale_mult=%.2f hp_override=%.0f dmg_mult=%.2f "
                         "blows=%u last=%.1f max=%.1f\n",
                         o, (int)(o == g_epic), game::health(o), game::max_health(o), game::scale(o), h, hr ? F<uint32_t>(h, 0x88) : 0,
                         hr ? F<int>(h, 0xF0) : -1, hr ? F<float>(h, 0x150) : 0.f, hr ? F<float>(h, 0x154) : 0.f, hr ? F<float>(h, 0x158) : 0.f,
                         d.n, d.last, d.max);
                u->s += b;
                return true;
            },
            &u);
        return u.s + "end";
    }
    if (a.size() >= 3 && a[0] == "epic" && a[1] == "near" && is_host()) { // epic near <member id>: the co-op epic next to it
        void* c = local_of((uint16_t)atoi(a[2].c_str()));
        if (!g_epic || !c || !epic_listed())
            return "no epic / member";
        net::Vec3 p = game::position(c);
        p.x += 6.f;
        game::teleport(g_epic, p, game::orientation(g_epic));
        return "epic moved next to #" + a[2];
    }
    if (a.size() >= 3 && a[0] == "epic" && a[1] == "on" && is_host()) { // epic on <member id> [dx]: tests (BUG-1104), the co-op
        // epic put onto the member (dx m off along x), quieted (its herd's idle: it stands there)
        void* c = local_of((uint16_t)atoi(a[2].c_str()));
        if (!g_epic || !c || !epic_listed())
            return "no epic / member";
        net::Vec3 p = game::position(c);
        p.x += a.size() > 3 ? (float)atof(a[3].c_str()) : 0.f;
        game::teleport(g_epic, p, game::orientation(g_epic));
        return "epic put on #" + a[2];
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "quiet") { // RE: quiet every epic herd of ours now
        std::string out;
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                if (noun == game::kHerdNoun && is_epic_herd_of_ours(o)) {
                    char b[96];
                    uint8_t e0 = F<uint8_t>(o, 0x218);
                    game::herd_quiet(o);
                    snprintf(b, sizeof b, "herd %p enabled %d -> %d (ours %p)\n", o, e0, F<uint8_t>(o, 0x218), g_epic_herd);
                    *(std::string*)u += b;
                }
                return true;
            },
            &out);
        return out.empty() ? "no herd of ours" : out;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "save") { // as if the game saved now (BUG-0121)
        before_save();
        return "before_save done";
    }
    if (!a.empty() && a[0] == "epic" && is_host()) {
        if (a.size() >= 3 && a[1] == "hit") { // epic hit <attacker id> <dmg>
            void* at = local_of((uint16_t)atoi(a[2].c_str()));
            if (!g_epic || !at || !epic_listed())
                return "no epic / member";
            return hit(g_epic, at, a.size() > 3 ? (float)atof(a[3].c_str()) : 50.f);
        }
        // F3 (BUG-0768): tribe.epic off: no co-op epic (`epic force` spawns one anyway, tests)
        if (!settings::on(settings::kTribeEpic) && !(a.size() >= 2 && a[1] == "force"))
            return "no epic: the session setting tribe.epic is off";
        bool ok = spawn_epic();
        uintptr_t is_epic = H->sym("SporeCoop::cCreatureAnimal_IsEpic");
        char b[200];
        // BUG-0563 (tests): the epic's species, and whether it is a player's creature (any player of the session)
        void* sp = g_epic ? game::species_profile(g_epic) : nullptr;
        int players = 0;
        for (auto& pl : C.sess->players())
            players += sp && creations::player_species(pl.id) == sp;
        snprintf(b, sizeof b, "epic %s %p is_epic=%d hp=%.0f species=%08X player_species=%d skipped=%u", ok ? "spawned" : "not spawned", g_epic,
                 g_epic && is_epic ? (int)thiscall<bool>(is_epic, g_epic) : -1, g_epic ? F<float>(g_epic, 0x5E0) : 0.f,
                 g_epic ? game::species_key(g_epic).instance : 0u, players, g_epic_skipped);
        return b;
    }
    if (a.size() >= 3 && a[0] == "select") { // select <id> <0|1>
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, atoi(a[2].c_str()));
        return std::string("selected=") + std::to_string(F<uint8_t>(c, kSelectedOff));
    }
    if (a.size() >= 3 && a[0] == "select_own") { // select_own <n> <0|1>: the player tribe's n-th member (tests:
        // a guest in a world of its own has no shared ids, BUG-0231)
        void* pt = player_tribe();
        auto ms = pt ? members(pt) : std::vector<void*>{};
        size_t n = (size_t)atoi(a[1].c_str());
        if (n >= ms.size())
            return "no such member";
        vcall<int>((uint8_t*)ms[n] + 0xC0, 0x54 / 4, atoi(a[2].c_str()));
        return std::string("selected=") + std::to_string(F<uint8_t>(ms[n], kSelectedOff));
    }
    if (a.size() >= 4 && a[0] == "walk") { // walk <x> <y> <z> : the selected members walk there
        float p[3] = {(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        thiscall<int>(a_WalkSelected, (void*)nullptr, (const float*)p);
        return "walk ordered";
    }
    if (!a.empty() && a[0] == "tools" && (a.size() < 2 || a[1] != "all")) { // BUG-0514: the player tribe's tools, and what the own members hold
        std::string s;
        struct U {
            std::string* s;
            int i;
            net::Vec3 c;
        } u{&s, 0, {}};
        center(u.c);
        for_each_player_tool(
            [](void* t, int type, const net::Vec3& p, void* up) {
                auto* u = (U*)up;
                char b[160];
                snprintf(b, sizeof b, "tool %d %p serial=%u type=%d pos=(%.2f %.2f %.2f) dist=%.1f\n", u->i++, t, game::object_serial(t), type,
                         p.x, p.y, p.z, net::dist(p, u->c));
                *u->s += b;
            },
            &u);
        void* pt = player_tribe();
        for (void* c : pt ? members(pt) : std::vector<void*>{}) {
            uint16_t id = member_id(c);
            char b[96];
            snprintf(b, sizeof b, "held %u owner=%u tool=%d leader=%d\n", id, owner_of(c), F<int>(c, kHeldToolOff),
                     (int)is_leader(c));
            s += b;
        }
        char b[192];
        snprintf(b, sizeof b, "tools n=%d tool_sets=%u grab_spot=%d moves=%u leader_kept=%u\n", u.i, g_tool_sets,
                 (int)g_grab_spot_fix, g_grab_spot_moves, g_leader_tools_kept);
        return s + b;
    }
    if (a.size() >= 2 && a[0] == "build" && is_host()) { // build <type> [metres]: test setup, a tool as if bought
        void* pt = player_tribe();
        net::Vec3 c;
        if (!pt || !center(c))
            return "no player tribe";
        float d = a.size() >= 3 ? (float)atof(a[2].c_str()) : 10.f;
        float l = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
        if (l < 1.f)
            return "no player tribe";
        net::Vec3 n{c.x / l, c.y / l, c.z / l};
        // a tangent: up x (an axis away from up)
        net::Vec3 ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
        net::Vec3 t{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
        float tl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        net::Vec3 p{c.x + t.x / tl * d, c.y + t.y / tl * d, c.z + t.z / tl * d};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        void* tool = make_player_tool(atoi(a[1].c_str()), p);
        char b[128];
        snprintf(b, sizeof b, "built %p type=%s at (%.2f %.2f %.2f)", tool, a[1].c_str(), p.x, p.y, p.z);
        return tool ? std::string(b) : std::string("build failed");
    }
    // BUG-0523 / BUG-0524 (tests): every tribe's tools with the host's index of their tribe
    if (a.size() >= 2 && a[0] == "tools" && a[1] == "all") {
        std::string s;
        for_each_tool(
            true,
            [](void* t, int type, int ix, bool player, const net::Vec3& p, void* up) {
                char b[160];
                snprintf(b, sizeof b, "atool %p serial=%u tribe=%d player=%d type=%d pos=(%.2f %.2f %.2f)\n", t, game::object_serial(t), ix,
                         (int)player, type, p.x, p.y, p.z);
                *(std::string*)up += b;
            },
            &s);
        char b[96];
        snprintf(b, sizeof b, "tribes=%u mapped=%u bought=%u\n", (unsigned)all_tribes().size(), (unsigned)g_guest.local_tribe_of.size(), g_tools_bought);
        return s + b;
    }
    // place <tribe index | p> <type> <metres> [degrees]: test setup, a tool for a tribe of this game (the host's index;
    // p the player tribe) that far from the tribe's centre, as a tribe's purchase makes it (no drag)
    // buy <type> <metres> [degrees]: the palette's placement in this game's player tribe (0xD13C05): the old tool of
    // the type goes, the new one is made and dragged (spatial +0x72) until `buy_end`, which sets the purchased bit
    if (a.size() >= 4 && (a[0] == "place" || a[0] == "buy")) {
        bool buy = a[0] == "buy";
        size_t o = buy ? 0 : 1;
        void* t = buy || a[1] == "p" ? player_tribe() : tribe_of_index(atoi(a[1].c_str()));
        if (!t)
            return "no such tribe";
        int type = atoi(a[1 + o].c_str());
        float d = (float)atof(a[2 + o].c_str()), deg = a.size() > 3 + o ? (float)atof(a[3 + o].c_str()) : 0.f;
        net::Vec3 c = tribe_pos(t);
        float l = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
        if (l < 1.f)
            return "no tribe position";
        net::Vec3 n{c.x / l, c.y / l, c.z / l};
        net::Vec3 ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
        net::Vec3 u{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        u = {u.x / ul, u.y / ul, u.z / ul};
        net::Vec3 w{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};
        float ca = std::cos(deg * 3.14159265f / 180.f), sa = std::sin(deg * 3.14159265f / 180.f);
        net::Vec3 p{c.x + (u.x * ca + w.x * sa) * d, c.y + (u.y * ca + w.y * sa) * d, c.z + (u.z * ca + w.z * sa) * d};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        void* tool = nullptr;
        if (buy) {
            std::vector<void*> old;
            for (void* x : tribe_tools(t))
                if (tool_type(x) == type)
                    old.push_back(x);
            tool = vcall<void*>(t, 0xB4 / 4, type);
            if (tool) {
                F<uint32_t>(tool, 0x84) |= 8;
                vcall<void>(tool, 0x64 / 4, t);
                thiscall<void>(a_ToolModelReset, tool);
                F<uint8_t>(tool, kToolDragOff) = 1;
                void* sp = game::cast(tool, 0x1186577);
                if (sp) {
                    float pp[3] = {p.x, p.y, p.z};
                    net::Quat r = upright(p);
                    float q[4] = {r.x, r.y, r.z, r.w};
                    vcall<void>(sp, 0x38 / 4, (const float*)pp);
                    vcall<void>(sp, 0x3C / 4, (const float*)q);
                }
                for (void* x : old)
                    vcall<void>(t, 0xB8 / 4, x);
                g_test_dragged = tool;
            }
        } else
            tool = make_tool_in(t, type, p);
        char b[160];
        snprintf(b, sizeof b, "%s %p type=%d at (%.2f %.2f %.2f)", buy ? "dragging" : "placed", tool, type, p.x, p.y, p.z);
        return tool ? std::string(b) : std::string("failed");
    }
    if (!a.empty() && a[0] == "buy_end") { // the end of `buy`'s drag: placed, the purchased bit (0xD10F90)
        void* pt = player_tribe();
        void* tool = g_test_dragged;
        g_test_dragged = nullptr;
        auto ts = tribe_tools(pt);
        if (!tool || std::find(ts.begin(), ts.end(), tool) == ts.end())
            return "nothing dragged";
        F<uint8_t>(tool, kToolDragOff) = 0;
        thiscall<void>(a_SetToolPurchased, pt, tool_type(tool));
        net::Vec3 p;
        game::object_position(tool, p);
        char b[128];
        snprintf(b, sizeof b, "placed %p type=%d at (%.2f %.2f %.2f)", tool, tool_type(tool), p.x, p.y, p.z);
        return b;
    }
    if (a.size() >= 2 && a[0] == "layout" && a_ToolLayout) { // layout <tool type>: the tribe's grab spots of a type
        void* pt = player_tribe();
        if (!pt)
            return "no player tribe";
        int type = atoi(a[1].c_str());
        uint8_t* l = thiscall<uint8_t*>(a_ToolLayout, (uint8_t*)pt + 0x394, &type);
        if (!l || !skg::readable(l, 0x58))
            return "no layout";
        uint8_t *sb = *(uint8_t**)(l + 0x50), *se = *(uint8_t**)(l + 0x54);
        char b[160];
        snprintf(b, sizeof b, "layout type=%d at=(%.1f %.1f %.1f) slots=%d\n", type, *(float*)(l + 0x14), *(float*)(l + 0x18),
                 *(float*)(l + 0x1C), sb && se > sb ? (int)((se - sb) / 0x18) : 0);
        std::string s = b;
        for (uint8_t* q = sb; q && q < se && skg::readable(q, 0x18); q += 0x18) {
            void* o = *(void**)(q + 4);
            net::Vec3 sp{*(float*)(q + 0xC), *(float*)(q + 0x10), *(float*)(q + 0x14)};
            snprintf(b, sizeof b, "  slot used=%u obj=%p member=%d pos=(%.1f %.1f %.1f) agl=%.2f\n", q[0], o,
                     is_citizen(o) ? (int)member_id(o) : -1, sp.x, sp.y, sp.z, drive::over_ground(sp)); // (BUG-1104: agl)
            s += b;
        }
        return s;
    }
    if (a.size() >= 5 && a[0] == "put" && is_host()) { // put <member id> <x> <y> <z>: test setup (BUG-0505), on the ground
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        net::Vec3 p{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        game::teleport(c, p, game::orientation(c));
        char b[96];
        snprintf(b, sizeof b, "put #%s at (%.1f %.1f %.1f)", a[1].c_str(), p.x, p.y, p.z);
        return b;
    }
    if (a.size() >= 3 && a[0] == "sink" && is_host()) { // sink <member id> <m>: test setup (BUG-1104), <m> under the ground once
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        net::Vec3 p = game::position(c);
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), m = (float)atof(a[2].c_str());
        if (g < 1.f || pl < 1.f)
            return "no ground";
        p = {p.x / pl * (g - m), p.y / pl * (g - m), p.z / pl * (g - m)};
        game::teleport(c, p, game::orientation(c));
        if (a.size() > 3 && a[3] == "air") { // not grounded, standing still (the airborne node's state, BUG-1082)
            game::set_grounded(c, false);
            F<net::Vec3>(c, game::kVelOff) = {0, 0, 0};
        }
        char b[128];
        snprintf(b, sizeof b, "sunk #%s to (%.2f %.2f %.2f), %.2f m over the ground", a[1].c_str(), p.x, p.y, p.z, drive::over_ground(p));
        return b;
    }
    // order <member id> <action> <x> <y> <z>: test setup (BUG-0531, host), the member's DoAction as an order gives it:
    // 4 walks there, 2 gathers the tree there, others take the object of this game nearest there (8 m)
    // (on a guest while the host is away too: its own game runs its members then, BUG-0879 tests)
    if (!a.empty() && a[0] == "conquests") { // BUG-0879 / BUG-0777 counters
        char b[160];
        snprintf(b, sizeof b, "conquests fix=%d sent=%u done=%u refused=%u local_play=%d\n", (int)g_away_conquest_fix, g_conquests_sent,
                 g_conquests_done, g_conquests_refused, (int)local_play());
        return b;
    }
    if (a.size() >= 6 && a[0] == "order" && (is_host() || local_play())) {
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        int action = atoi(a[2].c_str());
        net::Vec3 p{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
        if (!c)
            return "no such member";
        if (action == 4) {
            void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
            if (rec)
                thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)&p);
            return "walk ordered";
        }
        // [noun hex]: the object of that kind within 8 m of the point (a raid's hut 0x1E4DAAE, a village 0x18C6D19)
        uint32_t noun = a.size() >= 7 ? (uint32_t)strtoul(a[6].c_str(), nullptr, 16) : 0;
        void* tgt = noun ? game::find_object(noun, p, 8.f) : action == 2 ? find_tree(p, 3.f) : nullptr;
        if (!tgt)
            return "no target there";
        thiscall<void*>(a_DoAction, c, action, tgt, (void*)nullptr);
        return "ordered " + std::to_string(action);
    }
    // huthp <x> <y> <z> <hp>: tests (BUG-0531, host), the health of the hut within 10 m of the point (cCombatant at
    // +0x120, docs/re/tribe.md): a raid then ends in a few blows
    if (a.size() >= 5 && a[0] == "huthp" && (is_host() || local_play())) {
        net::Vec3 p{(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        void* h = game::find_object(0x01E4DAAE, p, 10.f);
        if (!h || !skg::readable(h, 0x160))
            return "no hut there";
        float& hp = F<float>(h, 0x120 + 0x38);
        char b[200];
        // BUG-0761: exact= the health unrounded; state= cCombatant +0x34 (2: dead, set by TakeDamage 0xBFCF10 when the
        // health goes under 1.5e-5, which posts the death message); last= +0x28 the last attacker (a cCombatant owner)
        snprintf(b, sizeof b, "hut %p health %.0f/%.0f exact=%.5f state=%d last=%p", h, hp, F<float>(h, 0x120 + 0x2C), hp,
                 F<int>(h, 0x120 + 0x34), F<void*>(h, 0x120 + 0x28));
        if (a.size() >= 5 && a[4] != "-")
            hp = (float)atof(a[4].c_str());
        return std::string(b) + " -> " + std::to_string((int)hp);
    }
    // hutfall <x> <y> <z>: tests (BUG-1076, host), the game's own killing blow on the hut within 10 m of the point
    // (as fell_hut, also while a cinematic plays): the players' own hut falls as when an NPC village raids it back
    if (a.size() >= 4 && a[0] == "hutfall" && (is_host() || local_play())) {
        net::Vec3 p{(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        void* h = game::find_object(0x01E4DAAE, p, 10.f);
        if (!h || !skg::readable(h, 0x160))
            return "no hut there";
        if (F<int>(h, 0x120 + 0x34) == 2)
            return "the hut is down already";
        static const float zero[3] = {0, 0, 0};
        float hp = F<float>(h, 0x120 + 0x38);
        vcall<int>((uint8_t*)h + 0x120, 0x18 / 4, hp + 10.f, 0xFFFFFFFFu, 0, zero, (void*)nullptr);
        char b[120];
        snprintf(b, sizeof b, "hut %p: blow of %.0f, state %d", h, hp + 10.f, F<int>(h, 0x120 + 0x34));
        return b;
    }
    if (a.size() >= 2 && a[0] == "who") { // who <citizen pointer>: BUG-0620, what a citizen of the object list is
        void* c = (void*)(uintptr_t)strtoul(a[1].c_str(), nullptr, 16);
        bool listed = false;
        game::for_each_object(
            [](void* o, uint32_t, void* u) {
                if (o == *(void**)((void**)u)[0])
                    *(bool*)((void**)u)[1] = true;
                return true;
            },
            (void*)(void* []){&c, &listed});
        if (!listed || !is_citizen(c))
            return "not a citizen of the object list";
        void* t = F<void*>(c, 0x1014);
        char b[200];
        int in_members = -1, n_tribes = 0, in_any = 0;
        for (void* tt : all_tribes()) {
            n_tribes++;
            auto m = members(tt);
            bool in = std::find(m.begin(), m.end(), c) != m.end();
            in_any += in;
            if (tt == t)
                in_members = in;
        }
        uint8_t ow = owner_of(c);
        snprintf(b, sizeof b, "tribe=%p (a tribe of the list: %s, in its members: %d) in any tribe's members: %d of %d tribes; "
                 "player tribe=%p; mod owner=%d id=%u; age=%u dead=%u species=%08X city=%p; ",
                 t, in_members >= 0 ? "yes" : "no", in_members, in_any, n_tribes, player_tribe(), ow == 0xFF ? -1 : ow, id_of(c),
                 F<uint8_t>(c, 0xB34), F<uint8_t>(c, 0xB5E), F<uint32_t>(c, 0xB28), F<void*>(c, 0x1010));
        return std::string(b) + ai_line(c);
    }
    if (a.size() >= 3 && a[0] == "frametool" && is_guest()) {
        g_frame_tool.ix = atoi(a[1].c_str());
        g_frame_tool.type = atoi(a[2].c_str());
        g_frame_tool.metres = a.size() >= 4 ? (float)atof(a[3].c_str()) : 10.f;
        g_frame_tool.armed = true;
        char b[96];
        snprintf(b, sizeof b, "frametool armed: tribe %d type %d (mapped=%u)", g_frame_tool.ix, g_frame_tool.type,
                 (unsigned)g_guest.local_tribe_of.size());
        return b;
    }
    if (a.size() >= 1 && a[0] == "reusehunt" && is_guest()) { // reusehunt [<mirror id> [frames]]: BUG-0844 (tests)
        if (a.size() < 2)
            return tribe_hunt_status();
        GEntry* it = g_guest.mirrors.find((uint16_t)atoi(a[1].c_str()));
        // any: the first mirror with a living member of a live tribe, alive on the host, seen now; npc: of a tribe that
        // is not the guest's player tribe (the new member is made in the player tribe: another tribe's)
        if (a[1] == "any" || a[1] == "npc") {
            it = nullptr;
            g_guest.mirrors.for_each([&](GEntry& e) {
                void* ec = e.ref.p;
                if (!it && ec && e.data.hp > 0 && !e.data.body && H->time_seconds() - e.data.seen < 1.0 &&
                    !g_detached.count(ec) && tribe_holding(ec) && !game::is_dead(ec) &&
                    (a[1] == "any" || tribe_holding(ec) != player_tribe()))
                    it = &e;
            });
        }
        void* c = it ? it->ref.p : nullptr;
        void* lt = c ? tribe_holding(c) : nullptr;
        if (!lt || g_detached.count(c))
            return "no mirror with a member of a live tribe\n";
        g_th = TribeHunt{};
        g_th.target = (uint8_t*)c;
        g_th.id = it->id;
        g_th.left = a.size() > 2 ? atoi(a[2].c_str()) : 150;
        vcall<void>(lt, 0x9C / 4, c, 1); // the game's removal (RemoveMember with destroy): the mirror still names it
        return "mirror's citizen removed by the game; " + tribe_hunt_status();
    }
    if (a.size() >= 2 && a[0] == "statehold" && is_guest()) { // statehold 1|0: tests (BUG-0672), the host's tribe
        // states are ignored (the mapping of host tribe index -> this game's tribe stays as it is, as before the first
        // state of a new world)
        g_state_hold = atoi(a[1].c_str()) != 0;
        char b[96];
        snprintf(b, sizeof b, "tribe states %s; mapped=%u stale_skipped=%u map=", g_state_hold ? "held" : "taken",
                 (unsigned)g_guest.local_tribe_of.size(), g_map_stale_skipped);
        std::string out = b;
        void* pt = player_tribe();
        for (size_t i = 0; i < g_guest.local_tribe_of.size() && i < 32; i++) {
            snprintf(b, sizeof b, "%u:%p%s ", (unsigned)i, g_guest.local_tribe_of[i], g_guest.local_tribe_of[i] && g_guest.local_tribe_of[i] == pt ? "(p)" : "");
            out += b;
        }
        return out;
    }
    if (a.size() >= 3 && a[0] == "localbt" && is_guest()) { // localbt <member id> 1|0: tests (BUG-0667), that mirror's
        // behaviour tree runs on this guest as while the host is away (local_play), or is skipped again
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such mirror";
        if (atoi(a[2].c_str()))
            g_bt_local.insert(c);
        else
            g_bt_local.erase(c);
        return std::string("mirror's tree ") + (g_bt_local.count(c) ? "runs" : "skipped");
    }
    if (a.size() >= 3 && a[0] == "freezeat" && is_guest()) { // freezeat <member id> <node hex> [state [ticks]]: tests
        // (BUG-1084), the mirror's tree is held in that node from the tick it is there in that state or a later one
        // (after that many more ticks) (while the host is away) until the host is back
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such mirror";
        g_bt_freeze_at[c] = {(uint32_t)strtoul(a[2].c_str(), nullptr, 16), a.size() >= 4 ? atoi(a[3].c_str()) : 0,
                             a.size() >= 5 ? atoi(a[4].c_str()) : 0};
        return "armed; held now: " + std::to_string(g_bt_frozen.size());
    }
    if (a.size() >= 2 && a[0] == "eat" && is_guest()) { // eat <member id>: tests (BUG-0667), the mirror's own Eat
        // (CitizenAction 0) at its tribe's food mat (+0x260) in this game, as a click on the mat gives it
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        void* t = c ? F<void*>(c, kOwnerTribeOff) : nullptr;
        void* mat = t ? F<void*>(t, 0x260) : nullptr;
        if (!mat)
            return "no such mirror or no food mat";
        void* rec = thiscall<void*>(a_DoAction, c, 0, mat, (void*)nullptr);
        return std::string("eat ordered (rec ") + (rec ? "made" : "none") + ")";
    }
    if (a.size() >= 2 && a[0] == "ai") { // ai <member id>: BUG-0505, the member's order record, node and walk
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        return c ? ai_line(c) : "no such member";
    }
    if (a.size() >= 2 && a[0] == "hand") { // hand <member id> [item [pending] | end]: tests (BUG-0906), the member's hand: what it
        // holds, its scale; with an item the game's attach again (the hand emptied first)
        void* c = is_guest() ? member_mirror((uint16_t)atoi(a[1].c_str())) : local_of((uint16_t)atoi(a[1].c_str()));
        if (!has_body(c))
            return "no such member";
        int cleared = -1;
        if (a.size() >= 3 && a[2] != "end" && a_SetHandheldItem) {
            // `pending` (tests, BUG-1349): the pending starts of the animation drawn now are ended first (the BUG-0992
            // call): the attach binds the hold to a pending track of the current id before the playing one, and a hold
            // on a track that does not play ends at once (tribe_mirror_hold: "the host's chief let go at once")
            if (a.size() >= 4 && a[3] == "pending") {
                uint32_t cur = 0;
                cleared = game::drawn_state(c, nullptr, &cur) && clear_pending_same(c, cur) ? 1 : 0;
            }
            thiscall<void>(a_SetHandheldItem, c, 0, -1);
            set_hand(c, atoi(a[2].c_str()));
            if (is_guest()) {
                hold_ev((uint16_t)atoi(a[1].c_str()), 'H', (uint32_t)atoi(a[2].c_str()), 0); // tests (BUG-0993): the hand set here
                g_hand_probe_id = (uint16_t)atoi(a[1].c_str());
                g_hand_probe_handle = 0;
                g_hand_probe_until = H->time_seconds() + 3.0;
            }
        }
        if (a.size() >= 3 && a[2] == "end")
            return "end " + std::to_string(hold_modifier(c, true) ? 1 : 0);
        char b[160];
        snprintf(b, sizeof b, "hand item=%d model=%d hand=%d scale=%.3f desired=%.3f sets=%u", F<int>(c, kHeldItemOff),
                 hand_model(c), F<int>(c, kHandOff), vcall<float>((uint8_t*)c + 0xC0, 0x34 / 4), vcall<float>(c, 0x80 / 4, 0),
                 g_item_sets);
        return std::string(b) + " hold=" + std::to_string(hold_modifier(c, false) ? 1 : 0) + " holds_ended=" +
               std::to_string(g_item_holds_ended) + (cleared >= 0 ? " pending_cleared=" + std::to_string(cleared) : "");
    }
    // BUG-1335 (tests): handbone [member id]: each member of the player tribe (or that one): its hand bone (+0xFC4),
    // its body's bone count, the bone's state against the array (ok / out / unknown), its item and the drawn model
    if (a.size() >= 1 && a[0] == "handbone") {
        auto line = [](void* c) {
            char b[200];
            int hand = F<int>(c, kHandOff);
            auto st = game::creature_bone(c, hand);
            snprintf(b, sizeof b, "#%u hand=%d bones=%d state=%s item=%d model=%d age=%d leader=%d\n", member_id(c), hand,
                     game::creature_bone_count(c), st == bone_rules::Bone::Ok ? "ok" : st == bone_rules::Bone::Out ? "out" : "unknown",
                     F<int>(c, kHeldItemOff), hand_model(c), (int)F<uint8_t>(c, kAgeOff), (int)is_leader(c));
            return std::string(b);
        };
        std::string out;
        if (a.size() >= 2) {
            void* c = is_guest() ? member_mirror((uint16_t)atoi(a[1].c_str())) : local_of((uint16_t)atoi(a[1].c_str()));
            out = has_body(c) ? line(c) : "no such member\n";
        } else if (void* pt = player_tribe())
            for (void* c : members(pt))
                if (has_body(c))
                    out += line(c);
        char b[160];
        snprintf(b, sizeof b, "fix=%d waits=%u repairs=%u fx_skips=%u\n", (int)game::fx_bone_fix(), g_hand_waits,
                 g_hand_repairs, game::fx_bone_skips());
        return out + b;
    }
    // hand_race [owner]: tests (BUG-1335), the lab's case on purpose: a member made in the player tribe (born for that
    // owner: its line species, tribe.birth_species) and made its tribe's chief in the same frame (cTribe_SetChieftain,
    // what tribe.leader_sync did 0.65 s after a mirror's spawn): its hand bone and its body's bones before the call.
    // A guest's such member has no host member: the next snapshot removes it
    if (a.size() >= 1 && a[0] == "hand_race" && a_SetChieftain) {
        void* pt = player_tribe();
        if (!pt)
            return "no player tribe";
        uint8_t owner = a.size() >= 2 ? (uint8_t)atoi(a[1].c_str()) : 0xFF;
        void* c = spawn_member(pt, 1, owner);
        if (!c)
            return "no member made";
        int hand = F<int>(c, kHandOff), bones = game::creature_bone_count(c);
        uint32_t skips = game::fx_bone_skips();
        auto st = game::creature_bone(c, hand);
        thiscall<void>(a_SetChieftain, pt, c);
        char b[220];
        snprintf(b, sizeof b, "race: member %p species %08X hand=%d bones=%d state=%s, made chief: item=%d fx_skips +%u", c,
                 F<uint32_t>(c, 0xB28), hand, bones, st == bone_rules::Bone::Ok ? "ok" : st == bone_rules::Bone::Out ? "out" : "unknown",
                 F<int>(c, kHeldItemOff), game::fx_bone_skips() - skips);
        return b;
    }
    // hand_plant <member id> [item]: tests (BUG-1335), the fault's state made on purpose on a member: its hand bone set
    // past its body's bone array, the item (default 0x11, the chief's staff) attached with the game's own call, the hand
    // bone put back. creature.fx_bone on: not made (counted), the item is attached once the bone is back (the repair);
    // off: the game's CreateEffect reads past the array (the lab's fault)
    if (a.size() >= 2 && a[0] == "hand_plant" && a_SetHandheldItem) {
        void* c = is_guest() ? member_mirror((uint16_t)atoi(a[1].c_str())) : local_of((uint16_t)atoi(a[1].c_str()));
        int bones = has_body(c) ? game::creature_bone_count(c) : -1;
        if (bones <= 0)
            return "no such member, or its body has no bones";
        int item = a.size() >= 3 ? atoi(a[2].c_str()) : 0x11;
        int hand = F<int>(c, kHandOff);
        uint32_t skips = game::fx_bone_skips();
        thiscall<void>(a_SetHandheldItem, c, 0, -1); // (the hand emptied at its own bone first)
        g_hand_plant = {c, game::object_serial(c), hand}; // (a fault unwinds past this frame: the next tick puts it back)
        F<int>(c, kHandOff) = bones + 0x4000;
        thiscall<void>(a_SetHandheldItem, c, item, -1);
        F<int>(c, kHandOff) = hand;
        g_hand_plant = {};
        char b[200];
        snprintf(b, sizeof b, "planted: hand %d -> %d (body %d bones) item %d, fx_skips +%u, item now %d model %d", hand,
                 bones + 0x4000, bones, item, game::fx_bone_skips() - skips, F<int>(c, kHeldItemOff), hand_model(c));
        return b;
    }
    if (a.size() >= 2 && a[0] == "bhvzero" && is_host()) { // bhvzero <member id>: tests (BUG-1071), the member as after
        // the save's load: no behaviour start seen yet (bhv_anim 0)
        if (HEntry* e = g_host.members.find((uint16_t)atoi(a[1].c_str()))) {
            e->data.bhv_anim = 0;
            return "bhvzero " + a[1];
        }
        return "no such member";
    }
    if (a.size() >= 3 && a[0] == "mplay" && is_guest()) { // mplay <member id> <hex anim>: tests (BUG-0972), the mirror
        // plays that animation as an earlier relayed start that came back after another one (not the last relayed)
        GEntry* it = g_guest.mirrors.find((uint16_t)atoi(a[1].c_str()));
        if (!it || !mirror_ok(*it) || !has_body(it->ref.p))
            return "no such mirror";
        bool ok = game::play_behaviour_anim(it->ref.p, (uint32_t)strtoul(a[2].c_str(), nullptr, 16), 0, 1, -1);
        uint32_t cur = 0;
        game::drawn_state(it->ref.p, nullptr, &cur);
        char b[96];
        snprintf(b, sizeof b, "mplay %d cur=%08X relayed=%08X", (int)ok, cur, it->data.relayed_anim);
        return b;
    }
    if (a.size() >= 3 && a[0] == "applytool" && is_guest()) { // applytool <member id> <type>: tests (BUG-0583), the
        // mirror's tool field applied as if the host's member held that tool (the tool field's own apply, BUG-0514)
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such mirror";
        apply_held_tool(c, (float)atoi(a[2].c_str()));
        return "tool " + std::to_string(F<int>(c, kHeldToolOff)) + " leader=" + std::to_string((int)is_leader(c));
    }
    if (a.size() >= 3 && a[0] == "hurt" && is_guest()) { // hurt <member id> <dmg>: tests (BUG-0685), a blow of this
        // game on the mirror (the citizen's TakeDamage, as a fight in the guest's own game)
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such mirror";
        return hit(c, nullptr, (float)atof(a[2].c_str()));
    }
    if (a.size() >= 2 && a[0] == "chief" && is_guest()) { // chief <member id>: tests (BUG-0652), this game's
        // cTribe::SetChieftain on the mirror (the state a load or a respawned mirror left: another leader here)
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        void* tribe = c && is_citizen(c) ? F<void*>(c, 0x1014) : nullptr;
        if (!tribe || !a_SetChieftain)
            return "no such mirror";
        thiscall<void>(a_SetChieftain, tribe, c);
        return "leader=" + std::to_string((int)is_leader(c)) + " tool " + std::to_string(F<int>(c, kHeldToolOff));
    }
    if (a.size() >= 6 && a[0] == "setvel" && is_host()) { // setvel <id> <vx> <vy> <vz> <s>: tests (BUG-0759), above
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        g_test_vel[c] = {{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())},
                         H->time_seconds() + atof(a[5].c_str())};
        return "velocity held";
    }
    if (a.size() >= 4 && a[0] == "hang" && is_host()) { // hang <id> <m> <s>: tests (BUG-1082), above
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        game::test_hang(c, (float)atof(a[2].c_str()), atof(a[3].c_str()));
        return "member held up";
    }
    // hunger <member id> <v>: tests (BUG-0935), the member's (host) or mirror's (guest) hunger through the game's setter,
    // as minutes of the game's own hunger update leave it (a guest's mirror that never eats: 0)
    // BUG-1187: `hunger <id>` only reads; `hunger <id> <v> end` ends the member's full clock first (a member just made
    // or just fed stays at 100 for its delay: the value would not hold); `full=` running / elapsed ms / delay ms
    if (a.size() >= 2 && a[0] == "hunger") {
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c || !has_body(c) || !a_SetHunger)
            return "no such member";
        bool ended = a.size() >= 4 && a[3] == "end" && end_full_delay(c);
        if (a.size() >= 3)
            thiscall<void>(a_SetHunger, c, (float)atof(a[2].c_str()));
        uint8_t* clk = (uint8_t*)c + kHungerClockOff;
        char b[200];
        snprintf(b, sizeof b, "hunger %f sets=%u fix=%d full=%d/%u/%u ended=%d full_ended=%u full_fix=%d", F<float>(c, kHungerOff),
                 g_hunger_sets, (int)g_hunger_fix, (int)clk[0x18], a_ClockElapsed ? (unsigned)thiscall<uint64_t>(a_ClockElapsed, clk) : 0u,
                 F<uint32_t>(c, kHungerDelayOff), (int)ended, g_hunger_full_ended, (int)g_hunger_full_fix);
        return b;
    }
    if (a.size() >= 3 && a[0] == "sethp" && is_host()) { // sethp <member id> <hp>: tests (BUG-0759), the member's health
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        game::set_health(c, (float)atof(a[2].c_str()));
        return "hp " + std::to_string(game::health(c));
    }
    if (a.size() >= 3 && a[0] == "settool" && is_host()) { // settool <member id> <type>: tests (BUG-0583), the game's
        // SetSpecializedTool on the host's member (any tribe's), as when it takes a rack's weapon
        HEntry* he = g_host.members.find((uint16_t)atoi(a[1].c_str()));
        void* c = he && hm_ok(*he) ? he->ref.p : nullptr;
        if (!c || !is_citizen(c) || !a_SetSpecializedTool)
            return "no such member";
        thiscall<void>(a_SetSpecializedTool, c, atoi(a[2].c_str()));
        return "tool " + std::to_string(F<int>(c, kHeldToolOff)) + " leader=" + std::to_string((int)is_leader(c));
    }
    if (a.size() >= 3 && a[0] == "grab") { // grab <member id> <tool index>: the member's GrabTool (3) order as a
        // click on the tool gives it (DoToolActionForAllSelected -> DoActionForObjects): a guest sends it to the host
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        int want = atoi(a[2].c_str());
        struct U {
            int i, want;
            void* tool;
        } u{0, want, nullptr};
        for_each_player_tool(
            [](void* t, int, const net::Vec3&, void* up) {
                auto* u = (U*)up;
                if (u->i++ == u->want)
                    u->tool = t;
            },
            &u);
        if (!c || !u.tool)
            return "no such member or tool";
        void* sel[1] = {(uint8_t*)c + 0xC0};
        void* vec[3] = {sel, sel + 1, sel + 1};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, 3, u.tool, (void*)nullptr);
        return "grab ordered (ret " + std::to_string(r) + ") " + g_last_cmd;
    }
    // fruit <x> <y> <z>: tests (BUG-0744), the selected members' Gather (2) on this game's cFruit nearest the point
    // (within 3 m), as a right-click on it gives (DoActionForObjects; a guest sends it to the host)
    if (a.size() >= 4 && a[0] == "fruit") {
        net::Vec3 p{(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        void* f = game::find_object(kFruitNoun, p, 3.f);
        if (!f)
            return "no fruit there";
        std::vector<void*> sel;
        g_guest.mirrors.for_each([&](GEntry& e_) {
            if (e_.ref.p && F<uint8_t>(e_.ref.p, kSelectedOff))
                sel.push_back((uint8_t*)e_.ref.p + 0xC0);
        });
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        });
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, 2, f, (void*)nullptr);
        return "gather ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ") " + g_last_cmd;
    }
    // unfruit <x> <y> <z> <r>: tests (BUG-0744, host), this game's cFruit within r m of the point destroyed (a fruit
    // the guest's game still shows there is then on the guest only)
    if (a.size() >= 5 && a[0] == "unfruit" && is_host()) {
        net::Vec3 p{(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        float r = (float)atof(a[4].c_str());
        struct Q {
            net::Vec3 p;
            float r;
            std::vector<void*> v;
        } q{p, r, {}};
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* q = (Q*)u;
                net::Vec3 op;
                if (noun == kFruitNoun && game::object_position(o, op) && net::dist(op, q->p) <= q->r)
                    q->v.push_back(o);
                return true;
            },
            &q);
        for (void* f : q.v)
            game::destroy_instance(f);
        size_t n = q.v.size();
        return "destroyed " + std::to_string(n) + " fruit; tribe.fruit_order moved " + std::to_string(g_fruit_orders_moved);
    }
    // duet: tests (BUG-0748), the player tribe's members in the social performance node 0xB46E8803 (host): id, owner, the
    // session's target tribe, its state, the mini-game's progress (ui +0x68)
    if (!a.empty() && a[0] == "duet") {
        std::string o;
        char b[160];
        for (auto& p : performers()) {
            float pr = p.ui && !IsBadReadPtr(p.ui, 0x6C) ? F<float>(p.ui, 0x68) : -1.f;
            snprintf(b, sizeof b, "performer #%u owner=%u target=%p state=%d ui=%p progress=%.3f\n", p.id, p.owner, p.target,
                     p.state, p.ui, pr);
            o += b;
        }
        return o + "duet fix=" + std::to_string((int)g_duet_fix) + " ticks=" + std::to_string(g_duet_ticks) + "\n";
    }
    // act_obj <action> <noun hex> <x> <y> <z>: tests (BUG-0748), the selected members' order on this game's object of
    // that noun nearest the point (8 m), through DoActionForObjects as a click gives (a guest sends it to the host)
    if (a.size() >= 6 && a[0] == "act_obj") {
        int action = atoi(a[1].c_str());
        uint32_t noun = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        net::Vec3 p{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
        void* t = game::find_object(noun, p, 8.f);
        if (!t)
            return "no such object there";
        if (act_needs_object(action) && is_citizen(t))
            return "action needs a tool or a hut";
        std::vector<void*> sel;
        g_guest.mirrors.for_each([&](GEntry& e_) {
            if (e_.ref.p && F<uint8_t>(e_.ref.p, kSelectedOff))
                sel.push_back((uint8_t*)e_.ref.p + 0xC0);
        });
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        });
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, action, t, (void*)nullptr);
        return "action ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ") " + g_last_cmd;
    }
    if (!a.empty() && a[0] == "party") { // party: tests (BUG-0646), the selected members party at the own fire pit
        // (a click on the fire pit: CitizenAction 12 Party with the tool of type 10, docs/re/tribe.md 2)
        void* pt = player_tribe();
        void* pit = nullptr;
        if (pt)
            for (void** it = F<void**>(pt, kToolsOff); it && it < F<void**>(pt, kToolsOff + 4); ++it)
                if (*it && tool_type(*it) == 10)
                    pit = *it;
        if (!pit)
            return "no fire pit";
        std::vector<void*> sel;
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        });
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, 12, pit, (void*)nullptr);
        return "party ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ")";
    }
    if (a.size() >= 3 && (a[0] == "act" || a[0] == "act_npc")) { // act <action> <member id | npc id>
        void* t = a[0] == "act" ? local_of((uint16_t)atoi(a[2].c_str())) : npc::proxy((uint16_t)atoi(a[2].c_str()));
        int action = atoi(a[1].c_str());
        // BUG-0260: the object actions (3 GrabTool, 12 Party, 15 RaidHuts, 24 Repair) take a tool or a
        // hut; with a citizen as the target DoAction read a field of the citizen as a tool's and
        // faulted (DoAction+0x78C, data 0x39C). The game's UI never pairs them (docs/re/tribe.md 2).
        if (a[0] == "act" && t && act_needs_object(action))
            return "action " + std::to_string(action) + " needs a tool or a hut, not a member (attack a member: 7)";
        std::vector<void*> sel;
        g_guest.mirrors.for_each([&](GEntry& e_) {
            if (e_.ref.p && F<uint8_t>(e_.ref.p, kSelectedOff))
                sel.push_back((uint8_t*)e_.ref.p + 0xC0);
        });
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        });
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, action, t, (void*)nullptr);
        return "action ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ")";
    }
    if (a.size() >= 2 && a[0] == "chief_away" && is_host()) { // chief_away <m> [deg]: tests (F2, BUG-0764), the host's own
        // chief put <m> metres from where it stands, on the ground (off the partners' view)
        auto it = g_chief.find(0);
        if (it == g_chief.end() || !it->second)
            return "no host chief";
        void* c = it->second;
        net::Vec3 p0 = game::position(c);
        float l = std::sqrt(p0.x * p0.x + p0.y * p0.y + p0.z * p0.z), d = (float)atof(a[1].c_str());
        if (l < 1.f)
            return "no position";
        net::Vec3 n{p0.x / l, p0.y / l, p0.z / l};
        net::Vec3 ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
        net::Vec3 u{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        u = {u.x / ul, u.y / ul, u.z / ul};
        net::Vec3 v{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x}; // [angle]: degrees from u
        float an = a.size() >= 3 ? (float)atof(a[2].c_str()) * 0.0174533f : 0.f, ca = std::cos(an), sa = std::sin(an);
        net::Vec3 p{p0.x + (u.x * ca + v.x * sa) * d, p0.y + (u.y * ca + v.y * sa) * d, p0.z + (u.z * ca + v.z * sa) * d};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        game::teleport(c, p, game::orientation(c));
        return "chief put " + std::to_string((int)d) + " m away";
    }
    if (a.size() >= 2 && a[0] == "near" && is_host()) { // near <ratio>: tests (BUG-0340), the later chief put that
        // part of the sum of the two chiefs' bounding radii from the first one, standing (no order)
        if (g_chief.size() < 2)
            return "no two chiefs";
        std::vector<std::pair<uint8_t, void*>> ch(g_chief.begin(), g_chief.end());
        std::sort(ch.begin(), ch.end());
        void *c0 = ch[0].second, *c1 = ch[1].second;
        float d = (float)atof(a[1].c_str()) * (game::bounding_radius(c0) + game::bounding_radius(c1));
        net::Vec3 p0 = game::position(c0);
        float l = std::sqrt(p0.x * p0.x + p0.y * p0.y + p0.z * p0.z);
        if (l < 1.f)
            return "no position";
        net::Vec3 n{p0.x / l, p0.y / l, p0.z / l};
        net::Vec3 ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
        net::Vec3 u{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        net::Vec3 p{p0.x + u.x / ul * d, p0.y + u.y / ul * d, p0.z + u.z / ul * d};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        game::teleport(c1, p, game::orientation(c1));
        char b[128];
        snprintf(b, sizeof b, "put player %u's chief %.1f m from player %u's (radii %.1f + %.1f)", ch[1].first, d, ch[0].first,
                 game::bounding_radius(c0), game::bounding_radius(c1));
        return b;
    }
    if (!a.empty() && a[0] == "stack" && is_host()) { // tests (BUG-0340): every chief onto the first one
        if (g_chief.empty())
            return "no chiefs";
        auto first = *std::min_element(g_chief.begin(), g_chief.end());
        net::Vec3 at = game::position(first.second);
        int n = 0;
        for (auto& [o, c] : g_chief)
            if (c != first.second) {
                game::teleport(c, at, game::orientation(c));
                n++;
            }
        return "stacked " + std::to_string(n) + " chiefs on the chief of player " + std::to_string(first.first);
    }
    if (a.size() >= 2 && a[0] == "delay" && is_guest()) { // delay <s>: tests (BUG-0481), the mirrors' interpolation delay
        g_test_delay = std::max(0.0, std::min(3.0, atof(a[1].c_str())));
        return "mirrors follow the host's states " + std::to_string(g_test_delay) + " s back";
    }
    if (a.size() >= 3 && a[0] == "undo" && is_guest()) { // undo <id> <s>: tests (BUG-0386), placings of a mirror undone
        GEntry* it = g_guest.mirrors.find((uint16_t)atoi(a[1].c_str()));
        if (!it || !it->ref.p)
            return "no mirror " + a[1];
        it->data.test_undo_until = H->time_seconds() + std::max(0.0, std::min(10.0, atof(a[2].c_str())));
        it->data.test_undo_at = game::position(it->ref.p);
        return "mirror " + a[1] + ": placings without the mark undone for " + a[2] + " s";
    }
    if (!a.empty() && a[0] == "reload") { // reload [arm]: tests (BUG-1054), the host's reload after the live Tribe change
        if (a.size() >= 2 && a[1] == "arm")
            world::host_reload_after_share();
        return "host_reloads=" + std::to_string(world::host_reloads()) + "\n";
    }
    if (a.size() >= 2 && a[0] == "cycle" && is_host()) { // cycle <n>: tests (BUG-1051), the host's snapshot counter as after a restart
        g_cycle = (uint16_t)strtoul(a[1].c_str(), nullptr, 0);
        return "tribe cycle=" + std::to_string(g_cycle) + "\n";
    }
    if (a.size() >= 2 && a[0] == "lift" && is_guest()) { // lift <m>: tests (BUG-0329), the host's members m higher
        g_test_lift = (float)atof(a[1].c_str());
        return "members of the host taken " + a[1] + " m further from the planet's centre";
    }
    if (a.size() >= 3 && a[0] == "color") { // color <tribe index> <hex colour id>: tests (BUG-0644), this game's village colour
        auto tribes = all_tribes();
        size_t i = (size_t)atoi(a[1].c_str());
        if (i >= tribes.size())
            return "no such tribe";
        F<uint32_t>(tribes[i], 0x234) = (uint32_t)strtoul(a[2].c_str(), nullptr, 16); // mIDColorID (+0x230 +4)
        memset((uint8_t*)tribes[i] + 0x254, 0, 12);                                 // mCachedColor
        return "color set";
    }
    if (!a.empty() && a[0] == "species") // BUG-1271: every member's species against its owner's own line
        return species_list();
    if (a.size() >= 2 && a[0] == "setspecies") { // setspecies <hex crt inst>: tests (BUG-0643), this game's player tribe
        // members get that species (cCreatureAnimal::SetSpecies, as the game does after an editor: 0xD5BFA0)
        uintptr_t set = H->sym("Simulator::cCreatureAnimal::SetSpecies");
        void* prof = game::species_by_key({(uint32_t)strtoul(a[1].c_str(), nullptr, 16), 0x2B978C46, 0x40626200});
        void* pt = player_tribe();
        if (!set || !prof || !pt)
            return "no species / tribe";
        int n = 0;
        if (a.size() >= 3) { // setspecies <hex> <member id>: that member only (BUG-1319 tests: a stray line key)
            unsigned id = (unsigned)atoi(a[2].c_str());
            for (void* c : members(pt))
                if (member_id(c) == id && F<int>(c, kAgeOff) == 1) { // (never a kid: BUG-1313)
                    thiscall<bool>(set, c, prof, 1, true);
                    n++;
                }
            return "species set on " + std::to_string(n) + " (member " + std::to_string(id) + ")";
        }
        for (void* c : members(pt)) {
            thiscall<bool>(set, c, prof, F<int>(c, kAgeOff), true);
            n++;
        }
        // the tribe's own species key (mSpeciesKeys +0x1904: a vector whose first key sits at +0x1920 here, runtime
        // 2026-11-07): the roster's portraits are of this species
        if (F<uint32_t>(pt, 0x1924) == 0x2B978C46)
            F<uint32_t>(pt, 0x1920) = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        return "species set on " + std::to_string(n);
    }
    if (a.size() >= 2 && a[0] == "level" && is_host()) { // level <0..7>: tests (BUG-0645), the stores of the hut upgrade
        void* pt = player_tribe();
        int lv = atoi(a[1].c_str());
        if (!pt || lv < 0 || lv > 7)
            return "no player tribe / level 0..7";
        F<int>(pt, 0x38) = lv; // 0xFF0260, as the upgrade 0xCDA4F0 sets it
        uintptr_t hudp = H->sym("SporeCoop::TribeHud_ptr");
        void* hud = hudp && skg::readable((void*)hudp, 4) ? *(void**)hudp : nullptr;
        if (hud && skg::readable(hud, 0x14) && a_MaxPop)
            F<int>(hud, 0x10) = thiscall<int>(a_MaxPop, pt);
        return "level " + std::to_string(lv);
    }
    if (a.size() >= 2 && a[0] == "spawn" && is_host()) { // spawn <n> [age]: test setup, n more adults (age 0: babies)
        void* pt = player_tribe();
        int n = 0, age = a.size() >= 3 ? atoi(a[2].c_str()) : 1; // BUG-0700: babies grow up in the host's game
        for (int i = 0; pt && i < atoi(a[1].c_str()) && i < 40; i++) {
            void* c = spawn_member(pt, age);
            if (!c)
                break;
            n++;
        }
        return "spawned " + std::to_string(n);
    }
    // npc_tribe <members> <metres>: tests (BUG-0381), the host's game forms an NPC tribe that far from the
    // village (as the game's tribe controller does during play: the guests' games do not)
    if (a.size() >= 3 && a[0] == "npc_tribe" && is_host() && a_SpawnNpcTribe) {
        void* pt = player_tribe();
        if (!pt)
            return "no player tribe";
        net::Vec3 c = tribe_pos(pt);
        float r = drive::speed(c), d = (float)atof(a[2].c_str());
        if (r < 1)
            return "no village position";
        // a direction across the surface: c x (0,1,0) (or x (1,0,0) at a pole), then back onto the sphere
        net::Vec3 u{c.z, 0, -c.x};
        if (drive::speed(u) < 1e-3f)
            u = {0, -c.z, c.y};
        float ul = drive::speed(u);
        net::Vec3 q{c.x + u.x / ul * d, c.y + u.y / ul * d, c.z + u.z / ul * d};
        float ql = drive::speed(q);
        skg::Vector3 pos{q.x / ql * r, q.y / ql * r, q.z / ql * r};
        void* t = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, 0, atoi(a[1].c_str()), 0, 0, (void*)nullptr);
        if (!t)
            return "SpawnNpcTribe returned null";
        net::Vec3 tp = tribe_pos(t);
        char b[160];
        snprintf(b, sizeof b, "npc tribe %p at (%.1f %.1f %.1f): %u members, %.0f m from the village", t, tp.x, tp.y, tp.z,
                 (unsigned)members(t).size(), net::dist(tp, c));
        return b;
    }
    // species_miss <n>: tests (BUG-1375), a guest's next n tries to copy a host NPC tribe read its species as missing (the
    // host's answer is then here at once): the retry after such a try decides when the copy comes
    if (a.size() >= 2 && a[0] == "species_miss") {
        g_species_miss_test = (uint32_t)std::max(0, atoi(a[1].c_str()));
        return "species_miss " + std::to_string(g_species_miss_test);
    }
    // npc_tribe_at <members> <x> <y> <z>: tests (BUG-0332), SpawnNpcTribe at exactly that position (as a guest makes its
    // copy of a host's new village, BUG-0381), with the game's ground there before and after
    if (a.size() >= 5 && a[0] == "npc_tribe_at" && a_SpawnNpcTribe) {
        net::Vec3 q{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
        float g0 = game::ground_radius(q);
        skg::Vector3 pos{q.x, q.y, q.z};
        void* t = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, 0, atoi(a[1].c_str()), 0, 0, (void*)nullptr);
        if (!t)
            return "SpawnNpcTribe returned null";
        net::Vec3 tp = tribe_pos(t);
        char b[200];
        snprintf(b, sizeof b, "npc tribe %p at (%.2f %.2f %.2f) r %.2f: %u members; ground before %.2f after %.2f, %.2f m from the given point",
                 t, tp.x, tp.y, tp.z, drive::speed(tp), (unsigned)members(t).size(), g0, game::ground_radius(q), net::dist(tp, q));
        return b;
    }
    // anims: tests (BUG-0569), the animation each member plays now as the game holds it (Anim::AnimatedCreature
    // vt 0x58 GetCurrentAnimation): host, its members by id (with the tribe index), guest, its mirrors by the host's id
    if (!a.empty() && a[0] == "holdtrace") { // tests (BUG-0968): the guest's last hold events, oldest first
        int want = a.size() > 1 ? atoi(a[1].c_str()) : -1;
        std::string o;
        char b[96];
        uint32_t n = std::min<uint32_t>(g_hold_ev_n, 4096);
        for (uint32_t i = g_hold_ev_n - n; i != g_hold_ev_n; i++) {
            const HoldEv& e = g_hold_ev[i % 4096];
            if (want >= 0 && e.id != want)
                continue;
            snprintf(b, sizeof b, "hev %.3f %u %c %08X %08X\n", e.t, e.id, e.what, e.a, e.b);
            o += b;
        }
        snprintf(b, sizeof b, "now %.3f\n", H->time_seconds());
        return o + b;
    }
    if (!a.empty() && a[0] == "anims") {
        std::string o;
        char b[224];
        auto line = [&](uint16_t id, void* c, uint32_t bhv, uint32_t idle) {
            uint32_t an = 0;
            if (c && game::object_alive(c) && game::drawn_state(c, nullptr, &an)) {
                net::Vec3 p = game::position(c);
                uint8_t* tr = main_track(c);
                snprintf(b, sizeof b, "cur %u anim=%08X bhv=%08X pos=(%.1f %.1f %.1f) idle=%08X hold=%d t=%.3f loop=%d now=%.3f a0=%.3f b0=%d\n", id, an,
                         bhv, p.x, p.y, p.z, idle, has_body(c) && hold_modifier(c, false) ? 1 : 0, tr ? *(double*)(tr + 0xA8) : -1.0,
                         tr ? (int)track_loops(tr) : -1, H->time_seconds(), tr ? *(float*)(tr + 0xA0) : 0.f, tr ? *(int*)(tr + 0xB0) : 0);
                o += b;
            }
        };
        if (is_host()) // bhv: the last animation its behaviour tree started (the game's call, game.cpp's listener)
            g_host.members.for_each([&](HEntry& e_) {
                void* const c = e_.ref.p;
                HMember& m = e_.data;
                line(e_.id, c, m.bhv_anim, m.idle_anim);
            });
        else
            g_guest.mirrors.for_each([&](GEntry& e_) {
                const uint16_t id = e_.id;
                GMember& g = e_.data;
                line(id, mirror_ok(e_) ? e_.ref.p : nullptr, g.relayed_anim, 0);
            });
        return o + "anims fix=1 sent=" + std::to_string(g_anim.anim_relay_sent) +
               " rx=" + std::to_string(g_anim.anim_relay_rx) + " played=" + std::to_string(g_anim.anim_relay_played) + " failed=" +
               std::to_string(g_anim.anim_relay_failed) + " snap_played=" + std::to_string(g_anim.anim_snap_played) + "\nair fix=" + std::to_string((int)g_mirror_air_fix) + " sent=" + std::to_string(g_anim.air_sent) +
               " rx=" + std::to_string(g_anim.air_rx) + " placed=" + std::to_string(g_anim.air_placed) + " landed=" + std::to_string(g_anim.air_landed) + " released=" + std::to_string(g_air_released) + " culled_lifts=" + std::to_string(game::air_culled_lifts()) + " hunger_sets=" + std::to_string(g_hunger_sets) + "\nidle fix=" + std::to_string((int)g_mirror_idle_fix) +
               " sent=" + std::to_string(g_anim.idle_sent) + " refreshed=" + std::to_string(g_anim.idle_refreshed) + " played=" + std::to_string(g_anim.idle_played) +
               " stale_cut=" + std::to_string(g_anim.stale_cut) + "\nphase fix=" + std::to_string((int)g_mirror_phase_fix) +
               " sent=" + std::to_string(g_anim.phase_sent) + " rx=" + std::to_string(g_anim.phase_rx) + " set=" + std::to_string(g_anim.phase_set) + " unlooped=" + std::to_string(g_anim.phase_unlooped) +
               " last_off=" + std::to_string(g_phase_last_off) + "\ntracks fix=" + std::to_string((int)g_phase_tracks_fix) +
               " sent=" + std::to_string(g_anim.tracks_sent) + " rx=" + std::to_string(g_anim.tracks_rx) + " set=" + std::to_string(g_anim.tracks_set) + "\nhold fix=" + std::to_string((int)g_mirror_hold_fix) + " sent=" + std::to_string(g_anim.hold_sent) + " rx=" + std::to_string(g_anim.hold_rx) +
               " bound=" + std::to_string(g_anim.hold_bound) + " item_sets=" + std::to_string(g_anim.hold_item_sets) + " anim_plays=" + std::to_string(g_anim.hold_anim_plays) + " queued=" + std::to_string(g_anim.hold_queued) + " retried=" + std::to_string(g_anim.hold_retried) +
               "\ntheme sent=" + std::to_string(g_anim.theme_sent) + " rx=" + std::to_string(g_anim.theme_rx) + " played=" + std::to_string(g_anim.theme_played) +
               "\nparty fix=" + std::to_string((int)g_party_fix) + " sent=" + std::to_string(g_anim.party_sent) + " rx=" +
               std::to_string(g_anim.party_rx) + " played=" + std::to_string(g_anim.party_played) + " stopped=" + std::to_string(g_anim.party_stopped) + " outside=" + std::to_string(g_party_stops_outside) +
               "\nbaby fix=" + std::to_string((int)g_guest_baby_fix) + " sent=" + std::to_string(g_babies_sent) + " made=" + std::to_string(g_babies_made) + " paired=" + std::to_string(g_babies_paired) +
               " local=" + std::to_string(g_babies_local) + " from=" + [] { char b[16]; snprintf(b, sizeof b, "%X", (unsigned)g_baby_from); return std::string(b); }() +
               "\nbaby_pair fix=" + std::to_string((int)g_baby_pair_fix) + " seen=" + std::to_string(g_buys_seen) + " own=" +
               std::to_string(g_buys_own) + " other=" + std::to_string(g_buys_other) + " none=" + std::to_string(g_buys_none) +
               " waiting=" + std::to_string(g_buys.size()) + " last " + g_baby_last;
    }
    if (!a.empty() && a[0] == "form_pace") // tests (BUG-0571): the last decision of tribe.form_pace
        return "form_pace fix=" + std::to_string((int)g_form_pace_fix) + " held=" + std::to_string(g_forms_paced) + " last: " +
               (g_pace_last.empty() ? "-" : g_pace_last);
    // form_species: tests (BUG-1233), what the NPC tribe controller's FormTribe reads before it forms (nothing formed)
    if (!a.empty() && a[0] == "form_species" && a_NpcCtrl) {
        void* ctrl = skg::cdecl_call<void*>(a_NpcCtrl);
        FormSpecies fs;
        if (!form_species_state(ctrl, fs))
            return "form_species: unreadable (no controller, property list or species list)";
        // BUG-1238: the species of the list (their key instance, species +0x504) and the archetypes of the property list
        std::string keys;
        void** lb = F<void**>(ctrl, 0xD0);
        for (int i = 0; i < fs.species && i < 16; i++) {
            char b[16];
            void* sp = lb[i];
            snprintf(b, sizeof b, "%s%08X", i ? "," : "", sp && skg::readable((uint8_t*)sp + 0x504, 4) ? F<uint32_t>(sp, 0x504) : 0u);
            keys += b;
        }
        char b[64];
        snprintf(b, sizeof b, " saved_keys=%d", (int)((F<uintptr_t>(ctrl, 0x80) - F<uintptr_t>(ctrl, 0x7C)) / 12));
        return "form_species fix=" + std::to_string((int)g_form_species_fix) + " held=" + std::to_string(g_forms_past) +
               " last_fix=" + std::to_string((int)g_last_species_fix) + " last=" + std::to_string(g_forms_last) + " fresh_fix=" + std::to_string((int)g_form_species_fresh_fix) +
               " fresh=" + std::to_string(g_forms_fresh) + " " +
               g_form_species_last + " keys=" + keys + b;
    }
    // form: tests (BUG-0483), this game's NPC tribe controller forms its next tribe now (what it does during play)
    if (!a.empty() && a[0] == "form" && o_Form && a_NpcCtrl) {
        void* ctrl = skg::cdecl_call<void*>(a_NpcCtrl);
        if (!ctrl)
            return "no NPC tribe controller";
        size_t before = all_tribes().size();
        // BUG-0587: the controller forms nothing while the next tribe's species model is not baked yet: it asks
        // IBakeManager to bake it and returns (0xCC6440: species = list +0xD0 [min(tribes - 1, size - 1)],
        // IBakeManager vt 0x1C(species + 0x504, 0) false). Said here, so a test sees why nothing was formed.
        std::string why;
        static uintptr_t a_Bake = H->sym("Editors::IBakeManager::Get");
        auto* lb = F<void**>(ctrl, 0xD0);
        auto* le = F<void**>(ctrl, 0xD4);
        if (a_Bake && lb && le > lb && before > 0) {
            size_t i = std::min(before - 1, (size_t)(le - lb) - 1);
            void* species = lb[i];
            void* bm = species ? skg::cdecl_call<void*>(a_Bake) : nullptr;
            if (bm) {
                bool baked = vcall<char>(bm, 0x1C / 4, (uint8_t*)species + 0x504, 0) != 0;
                char b[96];
                snprintf(b, sizeof b, " (next species %p %s)", species, baked ? "baked" : "not baked yet");
                why = b;
            }
        }
        // and what else it needs: fewer tribe nouns (alive or not) than its property list allows, a spawn marker
        // (noun 0x36BE27E with +0x108 == 0x57F09E5E) left
        struct Cnt {
            int nouns = 0, markers = 0;
        } cnt;
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* c = (Cnt*)u;
                if (noun == kTribeNoun)
                    c->nouns++;
                else if (noun == 0x36BE27E && F<uint32_t>(o, 0x108) == 0x57F09E5E)
                    c->markers++;
                return true;
            },
            &cnt);
        why += " (tribe nouns " + std::to_string(cnt.nouns) + ", tribe spawn markers " + std::to_string(cnt.markers) + ")";
        // BUG-1233: what FormTribe itself reads: its tribe nouns against the property list and its species list (with
        // as many tribes as species it reads past the list: tribe.form_species holds it)
        FormSpecies fs;
        why += form_species_state(ctrl, fs) ? " (" + g_form_species_last + ")" : " (species list: unreadable)";
        uint32_t blocked0 = g_forms_blocked, past0 = g_forms_past, last0 = g_forms_last;
        form_species_recheck(); // decided for this call, not by the update's last second
        h_Form(ctrl, 0);
        return "tribes " + std::to_string(before) + " -> " + std::to_string(all_tribes().size()) + why +
               (g_forms_blocked != blocked0 ? " (blocked: tribe.guest_tribes)" : "") +
               (g_forms_past != past0 ? " (held: tribe.form_species)" : "") +
               (g_forms_last != last0 ? " (last species: tribe.form_last_species)" : "") +
               (g_pace_last.empty() ? "" : " (pace: " + g_pace_last + ")");
    }
    // markers: tests (BUG-0332), the NPC tribe spawn markers of this game (noun 0x36BE27E, +0x108 == 0x57F09E5E: where the
    // tribe controller forms the next tribes) with their positions
    if (!a.empty() && a[0] == "markers") {
        std::string out;
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                if (noun == 0x36BE27E && F<uint32_t>(o, 0x108) == 0x57F09E5E) {
                    // a placed planet object: its position at +0x1A8 (docs/re/host-centric.md, the activation pass)
                    const float* q = (const float*)((uint8_t*)o + 0x1A8);
                    char b[96];
                    snprintf(b, sizeof b, "marker %p pos=(%.2f %.2f %.2f)\n", o, q[0], q[1], q[2]);
                    *(std::string*)u += b;
                }
                return true;
            },
            &out);
        return (out.empty() ? "no markers\n" : out) + "markers_used=" + std::to_string(g_markers_used) + "\n";
    }
    if (a.size() >= 2 && a[0] == "savekill" && is_guest()) { // savekill on|off (tests, BUG-1522): the next save's end
        took_debug_kill(a[1] == "on");                         // first removes a member mirror it took out (a host state)
        return took_status();
    }
    if (!a.empty() && a[0] == "tribelists") { // tribelists (tests, BUG-1522): every tribe's member (+0x340) and selectable
        // (+0x354) vector: entries, those the game's list no longer has (freed), and on a guest the player tribe's
        // members no mirror is bound to (orphans)
        std::string out;
        void* pt = player_tribe();
        int ti = 0;
        for (void* t : all_tribes()) {
            int n[2] = {0, 0}, unl[2] = {0, 0}, orphan = 0;
            const uintptr_t offs[2] = {kMembersOff, 0x354};
            for (int k = 0; k < 2; k++) {
                void** b = F<void**>(t, offs[k]);
                void** e = F<void**>(t, offs[k] + 4);
                for (void** it = b; it && it < e && e - b < 1024; ++it) {
                    void* c = *it ? (k == 0 ? *it : (uint8_t*)*it - 0xC0) : nullptr;
                    n[k]++;
                    if (!c || !game::listed(c, 0x18EB4B7u))
                        unl[k]++;
                    else if (k == 0 && t == pt && is_guest() && !g_guest.mirrors.bound(c))
                        orphan++;
                }
            }
            char b[200];
            snprintf(b, sizeof b, "tribe %d%s members=%d unlisted=%d selectable=%d unlisted=%d orphans=%d\n", ti++,
                     t == pt ? " (player)" : "", n[0], unl[0], n[1], unl[1], orphan);
            out += b;
        }
        return out + took_status();
    }
    if (a.size() >= 5 && a[0] == "untree") { // untree <x y z> <r>: tests (BUG-1494), every tree within r m of the point is
        // picked empty here (cTreeData +0x48, the fruit left, = 0; +0x4C, its slots, stays): the trees and their fields
        net::Vec3 at{(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        float r = (float)atof(a[4].c_str());
        std::string out;
        int n = 0;
        for_each_tree([&](void* t, const net::Vec3& p) {
            float d = net::dist(p, at);
            if (d > r)
                return;
            char b[128];
            snprintf(b, sizeof b, "tree %p d=%.1f 48=%08X 4c=%08X -> 0\n", t, d, F<uint32_t>(t, 0x48), F<uint32_t>(t, 0x4C));
            out += b;
            F<uint32_t>(t, 0x48) = 0;
            n++;
        });
        return out + "emptied " + std::to_string(n) + " trees\n";
    }
    if (!a.empty() && a[0] == "trees") { // trees near the village with their raw fields (RE)
        net::Vec3 at;
        center(at);
        std::vector<std::pair<float, std::string>> v;
        for_each_tree([&](void* t, const net::Vec3& p) {
            char b[256];
            float d = net::dist(p, at);
            snprintf(b, sizeof b, "%p d=%.0f pos=(%.1f %.1f %.1f) c=%04X e=%04X 1c=%.2f 48=%08X 4c=%08X 50=%08X 54=%08X 58=%08X 6c=%.2f 78=%08X 7c=%02X\n",
                     t, d, p.x, p.y, p.z, F<uint16_t>(t, 0xC), F<uint16_t>(t, 0xE), F<float>(t, 0x1C), F<uint32_t>(t, 0x48),
                     F<uint32_t>(t, 0x4C), F<uint32_t>(t, 0x50), F<uint32_t>(t, 0x54), F<uint32_t>(t, 0x58), F<float>(t, 0x6C),
                     F<uint32_t>(t, 0x78), F<uint8_t>(t, 0x7C));
            v.push_back({d, b});
        });
        std::sort(v.begin(), v.end());
        std::string out;
        for (size_t i = 0; i < v.size() && i < (a.size() > 1 ? (size_t)atoi(a[1].c_str()) : 25); i++)
            out += v[i].second;
        return out;
    }
    if (a.size() >= 2 && a[0] == "objs") { // objs <noun hex>: objects of a kind near the village, world positions
        struct Q {
            uint32_t noun;
            net::Vec3 at;
            std::vector<std::pair<float, std::string>> v;
        } q{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), {}, {}};
        center(q.at);
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto* q = (Q*)u;
                net::Vec3 p;
                if (n == q->noun && game::object_position(o, p)) {
                    char b[128];
                    float d = net::dist(p, q->at);
                    snprintf(b, sizeof b, "%p alive=%d pos=(%.1f %.1f %.1f) dist=%.1f\n", o, (int)game::object_alive(o), p.x,
                             p.y, p.z, d);
                    q->v.push_back({d, b});
                }
                return true;
            },
            &q);
        std::sort(q.v.begin(), q.v.end());
        std::string out;
        for (size_t i = 0; i < q.v.size() && i < 40; i++)
            out += q.v[i].second;
        return out + std::to_string(q.v.size()) + " objects";
    }
    return "usage: coop.tribe select <id> <0|1> | select_own <n> <0|1> | walk <x> <y> <z> | act <action> <target id> | objs <noun hex>";
}
std::string list()
{
    std::string out;
    char b[512];
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t ti = 0; ti < tribes.size(); ti++) {
        net::Vec3 p = tribe_pos(tribes[ti]);
        auto ms = members(tribes[ti]);
        TribeInfo ti_info = tribe_info(tribes[ti]); // the members' species (BUG-0381 tests)
        uintptr_t hudp = tribes[ti] == pt ? H->sym("SporeCoop::TribeHud_ptr") : 0; // BUG-0645: the HUD's cap
        void* hud = hudp && skg::readable((void*)hudp, 4) ? *(void**)hudp : nullptr;
        snprintf(b, sizeof b, "tribe %u%s at (%.0f %.0f %.0f): %u members species=%08X archetype=%d made=%d level=%d cap=%d "
                 "hud_cap=%d obj=%p color=%08X stamp=%u\n",
                 (unsigned)ti, tribes[ti] == pt ? " (player)" : "", p.x, p.y, p.z, (unsigned)ms.size(), ti_info.key.instance,
                 ti_info.arch == 0xFF ? -1 : (int)ti_info.arch, (int)ours_in(g_ours_t, tribes[ti]), F<int>(tribes[ti], 0x38),
                 a_MaxPop ? thiscall<int>(a_MaxPop, tribes[ti]) : -1,
                 hud && skg::readable(hud, 0x14) ? F<int>(hud, 0x10) : -1, tribes[ti], F<uint32_t>(tribes[ti], 0x234),
                 F<uint32_t>(tribes[ti], kTribeStampOff)); // (BUG-0947: the tribe's ground stamp, 0 none)
        out += b;
        for (void* c : ms) {
            net::Vec3 cp = game::position(c);
            uint8_t ow = owner_of(c);
            uint16_t id = 0;
            id = member_id(c);
            net::Vec3 v = game::velocity(c), sv{};
            const drive::State* ds = nullptr;
            if (C.sess && C.sess->role() != net::Role::Host) { // the host's velocity in the snapshot
                if (GEntry* it = g_guest.mirrors.find(id)) {
                    sv = it->data.vel;
                    ds = &it->data.ds;
                }
            }
            snprintf(b, sizeof b,
                     "  #%u %p owner=%d age=%u sel=%u hp=%.1f pos=(%.1f %.1f %.1f) speed=%.1f host_speed=%.1f dead=%d bt=%08X radius=%.2f ours=%d tool=%d leader=%d item=%d model=%d hold=%d iscale=%.3f scale=%.3f maxhp=%.1f hunger=%.2f/%.2f/%.2f idle=%08X anim=%08X\n",
                     id, c, ow == 0xFF ? -1 : (int)ow, F<uint8_t>(c, kAgeOff), F<uint8_t>(c, kSelectedOff),
                     game::health(c), cp.x, cp.y, cp.z, drive::speed(v), drive::speed(sv), (int)game::is_dead(c),
                     bt_node(c), game::bounding_radius(c), (int)ours_in(g_ours_c, c), F<int>(c, kHeldToolOff), (int)is_leader(c),
                     F<int>(c, kHeldItemOff), hand_model(c), hold_modifier(c, false) ? 1 : 0, read_item_scale(c), game::scale(c), game::max_health(c),
                     F<float>(c, 0xBBC), F<float>(c, 0xBB8), F<float>(c, 0xB90), F<uint32_t>(c, 0xE3C), cur_anim(c)); // (BUG-0906: what the hand holds,
            // whether a model is attached, 1 while the item is still being taken: not in the resting grip)
            out += b;
            if (ds) { // guests: how the mirror is driven (drive::step)
                snprintf(b, sizeof b, "     drive err=%.2f nudges=%u kept=%u teleports=%u\n", ds->last_err, ds->nudges,
                         ds->nudges_kept, ds->teleports);
                out += b;
            }
        }
    }
    return out;
}

} // namespace tribe
