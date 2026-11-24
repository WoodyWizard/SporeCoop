// The console commands of the coop core (B2 of 0.7.15: moved out of coop.cpp, docs/audit-0.7.15/core.md CORE-2): the c_*
// functions and the command registrations of sporekit_mod_main, which calls the register_commands_* functions at the
// places the registrations had (the same order: SporeKit keeps the commands by name, the module inits between them stay
// where they were). Commands that are the whole interface of a recorder (coop.track, coop.bodyrec) stay next to it.
#include "lineage_cells.h"
#include "civ_wallet.h"
#include "coop_state.h"
#include "designs.h"
#include "own_progress.h"
#include "revive.h"
#include "cocoon.h"
#include "fov.h"
#include "editor_depth.h"
#include "net/compat.h"
#include "net/kick.h"
#include "net/manifest.h"

namespace coop_core {

// tests (BUG-0107): end the process the way the game's own fault handler does, through ExitProcess
// without cSporeApp::Shutdown. An abort on the way out (std::terminate from a static destructor)
// writes "abort" into mods\logs\coop_exittest.txt.
char g_exittest_path[MAX_PATH];
void exittest_abort(int)
{
    if (FILE* f = fopen(g_exittest_path, "a")) {
        fputs("abort\n", f);
        fclose(f);
    }
    _exit(3);
}

int c_exittest(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "old")) { // the old global std::thread, started now
        keepalive_stop();
        g_keepalive_ptr_fix = false;
        keepalive_start();
    }
    snprintf(g_exittest_path, sizeof g_exittest_path, "%s\\logs\\coop_exittest.txt", H->mods_dir());
    if (FILE* f = fopen(g_exittest_path, "w")) {
        fputs("exit 0\n", f);
        fclose(f);
    }
    signal(SIGABRT, exittest_abort);
    LOG("coop.exittest: ExitProcess(0) without Shutdown (keepalive %s)", g_keepalive ? "pointer" : "global object");
    ExitProcess(0);
}

// ---------------- commands ----------------
int c_host(void*, int argc, const char* const* argv, SkOut* out)
{
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : default_port();
    if (argc > 2)
        g_name = utf8::player_name(argv[2]);
    std::string err;
    if (!do_host(port, err)) {
        SAY("host failed: %s", err.c_str());
        // BUG-0302: in the game log too (a lab start whose host never hosted left no trace there)
        H->log(M, SK_WARN, "net: host failed on UDP port %u: %s", port, err.c_str());
        return 1;
    }
    SAY("hosting on UDP %u as '%s'", g_sess.port(), g_name.c_str());
    return 0;
}

int c_join(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc < 2) {
        SAY("usage: coop.join <ip[:port]> [name]");
        return 1;
    }
    if (argc > 2)
        g_name = utf8::player_name(argv[2]);
    std::string err;
    if (!do_join(argv[1], err)) {
        SAY("join failed: %s", err.c_str());
        return 1;
    }
    SAY("joining %s as '%s'", argv[1], g_name.c_str());
    return 0;
}

int c_leave(void*, int, const char* const*, SkOut* out)
{
    do_leave();
    SAY("left session");
    return 0;
}

int c_status(void*, int, const char* const*, SkOut* out)
{
    SAY("%s%sbusy: %s\n", g_sess.status().c_str(), world::status().c_str(), busy::names(game_busy()).c_str());
    SAY("%s", host_busy_status().c_str()); // C3 (BUG-1142)
    for (auto& pl : g_sess.players()) // CAP_VERSION: each player's mod, SporeKit and game build
        SAY("version %u '%s': %s\n", pl.id, pl.name.c_str(), pl.version.empty() ? "unknown (pre-0.7.4)" : pl.version.c_str());
    SAY("remote animations: received %u, played %u (behaviour %u); own behaviour sent %u\n", g_anims_rx, g_anims_played,
        g_bhv_anims_played, g_own_bhv_anims); // BUG-1253
    SAY("appeared notices: told %u, puppet rebuilds not told %u (BUG-0923)\n", g_appeared_told, g_appeared_quiet);
    SAY("%s", effects::status().c_str()); // BUG-0451
    SAY("%s", npc::status().c_str());
    SAY("%s", social::status().c_str());
    SAY("%s", tribe::status().c_str());
    SAY("%s", cellsync::status().c_str());
    SAY("%s", notify::status().c_str());
    SAY("dna notices: shown=%u gathering=%u\n", g_dna_noticed, (unsigned)g_dna_notices.open());
    {
        int sw = 0, sh = 0;
        H->screen_size(&sw, &sh);
        SAY("status block (%d %d %d %d) drawn to x %d of %d\n", g_status_block[0], g_status_block[1], g_status_block[2],
            g_status_block[3], g_status_drawn_right, sw);
    }
    SAY("%s", creations::status().c_str());
    SAY("%s", civ::status().c_str());
    SAY("%s", squadron::status().c_str());
    SAY("%s", spacecoop::status().c_str());
    SAY("%s", gamefixes::status().c_str());
    SAY("dna=%.1f (rx %u totals, forwarded %u gains) solo=%.1f owed=%.1f owed_paid=%u away=%d\n", game::dna(), g_dna_rx,
        g_dna_fwd, g_solo_dna, g_dna_owed, g_dna_owed_paid, (int)g_host_away);
    SAY("dna spend: unsent=%.1f sent=%u got=%u owed=%.1f seen=%.1f/%d\n", g_dna_spend, g_dna_spends_sent, g_dna_spends_got,
        g_dna_spend_owed, g_dna_seen, (int)g_dna_seen_ok); // BUG-0466
    SAY("dna pace: div=%d setting=%d heard=%d paced=%u in=%.1f out=%.1f\n", pace_div(), settings::get(settings::kDnaPace),
        g_pace_heard, g_paced, g_paced_in, g_paced_out); // BUG-0510
    SAY("%s", own_dna_status().c_str()); // WP-D (BUG-1291)
    for (size_t i = g_chat.size() > 6 ? g_chat.size() - 6 : 0; i < g_chat.size(); i++)
        SAY("chat: %s\n", g_chat[i].c_str());
    void* av = g_local;
    SAY("mode=0x%08X local=%p overlay-self=(%.1f %.1f ok=%.0f)\n", game::mode_id(), av, g_dbg_sc[0], g_dbg_sc[1], g_dbg_sc[2]);
    SAY("host_away=%d (times %u) solo_dna=%.1f solo_stale_totals=%u solo_dropped=%u kept_in_editor=%u\n", (int)g_host_away,
        g_host_away_count, g_solo_dna, g_solo_stale_totals, g_solo_dropped, g_away_kept_in_editor); // BUG-1422
    SAY("host elsewhere=%d, its world's messages dropped %u, dna pending %.1f, own-world dna gains %u, welcome world %08X, share retries %u\n",
        (int)(g_world_gate_fix ? guest_elsewhere() : host_elsewhere()), g_place_dropped, g_dna_pending, g_dna_own,
        g_sess.welcome_world(), g_share_retries); // BUG-0130, BUG-0231
    SAY("host save: known %08X (welcome %08X, followed %08X, heard %u), mine %08X, space_other=%d, announced %08X\n", host_save_known(),
        g_sess.welcome_world(), g_host_save, g_host_save_heard, g_mode == modes::kSpace ? modes::current_world() : 0,
        (int)guest_space_other(), g_world_announced); // BUG-0933
    SAY("own world: mode %08X world %08X elsewhere=%d space_other=%d host_left=%d\n", g_mode, g_world,
        (int)own_world::elsewhere(), (int)own_world::space_other(), (int)own_world::host_left()); // O1 (BUG-0427)
    SAY("space save: %s, load shares %u\n", g_space_key.empty() ? "-" : g_space_key.c_str(), g_space_load_shares); // BUG-0941
    {
        unsigned checking = 0; // BUG-1170: guests kept in place whose 30 s species check still runs
        for (auto& [id, r] : g_remotes)
            checking += r.evo_check_until > 0;
        SAY("evolve in place: kept %u, sent after all %u, saves %u, checking %u\n", g_evo_inplace, g_evo_fallback,
            g_evo_saves, checking); // BUG-0508
    }
    SAY("away dna: solo_mode=%08X solo_dropped=%u owed_world=%08X owed_dropped=%u spend_owed_world=%08X spend_dropped=%u\n",
        g_solo_mode, g_solo_dropped, g_dna_owed_world, g_owed_dropped, g_dna_spend_owed_world,
        g_spend_owed_dropped); // BUG-0233, BUG-1506
    SAY("strays: pending=%u removed=%u\n", (unsigned)g_strays.size(), g_strays_removed);
    SAY("labels: menu_up=%d window=%d hidden_frames=%u cinematic_frames=%u name_tags=%u\n", (int)g_menu_up.load(),
        (int)coop_ui::is_open(), g_menu_hidden, g_cine_hidden, g_tags_drawn);
    for (auto& p : g_sess.players()) // tests (BUG-0809): the datagrams sent to each peer so far
        if (net::Connection* c = p.id != g_sess.my_id() ? g_sess.conn_to(p.id) : nullptr)
            SAY("net to %u: out_bytes=%llu out_packets=%u\n", (unsigned)p.id, (unsigned long long)c->stats.bytes_sent,
                c->stats.packets_sent);
    double now = H->time_seconds();
    for (auto& [id, r] : g_remotes) {
        net::PlayerState s;
        bool have = r.buf.sample(now, s);
        SAY("remote %u '%s' puppet=%p state=%s age=%.2fs pos=(%.1f %.1f %.1f)\n", id, r.name.c_str(),
            r.puppet, have ? "yes" : "no", r.buf.age(now), s.pos.x, s.pos.y, s.pos.z);
        // tests parse the "puppet pos" line as the one right after the "remote" line: keep it there
        // BUG-0271: a cell puppet of a pool the game freed (the stage is being left) is not read
        if (puppet_now(r)) {
            net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
            float rot_err = -1;
            if (g_mode == modes::kCell) { // facing error in degrees
                net::Quat q;
                modes::mat_to_quat((float*)((uint8_t*)r.puppet + 0x5C), q);
                float dot = std::fabs(q.x * s.rot.x + q.y * s.rot.y + q.z * s.rot.z + q.w * s.rot.w);
                rot_err = 2.f * std::acos(std::min(1.f, dot)) * 57.2958f;
            }
            SAY("   puppet pos=(%.1f %.1f %.1f) err=%.2f rot_err=%.1f species=%08X", p.x, p.y, p.z,
                net::dist(p, s.pos), rot_err,
                g_mode == modes::kCreature || g_mode == modes::kScenario ? game::species_key(r.puppet).instance
                : g_mode == modes::kCell   ? *(uint32_t*)((uint8_t*)r.puppet + 0xFC) // cell model
                                           : 0u);
            if (g_mode == modes::kScenario) // tests (BUG-1254): the puppet's size against the partner's captain
                SAY(" scale=%.3f", game::scale(r.puppet));
            SAY("\n");
            if (g_mode == modes::kCreature)
                SAY("   drive err=%.2f speed=%.2f nudges=%u kept=%u teleports=%u walk_goals=%u jumps=%u airs=%u stop_holds=%u\n", r.drv.last_err,
                    r.drv.last_speed, r.drv.nudges, r.drv.nudges_kept, r.drv.teleports, r.drv.goals, r.drv.jumps, r.drv.airs, r.drv.stop_holds);
        }
        // tests (BUG-0507): the puppet the mod holds is a live creature in the game's own object list
        if (r.puppet && creature_puppet(r.puppet_adapter))
            SAY("puppet_listed %u %d gone_by_game=%u\n", id, (int)game::creature_listed(r.puppet), modes::g_puppets_gone_alive);
        // tests: both players are in the same world and place right now
        SAY("together %u %d\n", id, !r.buf.empty() && r.buf.age(now) < 3.0 && same_place(r.buf.latest()) ? 1 : 0);
        // tests (BUG-1368): the host still owes this guest its world (a share due, being saved, sent or acknowledged):
        // the guest reloads soon, also when it is together now (a rejoin into the world it had)
        if (g_sess.role() == net::Role::Host)
            SAY("world_due %u %d\n", id, r.needs_world || world::share_in_flight(id) ? 1 : 0);
        // tests (BUG-1435): this guest has yet to load the world sent last; the waits so far (ended by its load / by a
        // timeout), the shares asked for it
        if (g_sess.role() == net::Role::Host)
            SAY("load_wait %u %d loads=%u timeouts=%u shares=%u\n", id, r.load_wait ? 1 : 0, g_load_waits,
                g_load_wait_timeouts, world::share_serial(id));
    }
    return 0;
}

// BUG-0164: capped like the co-op window (24 characters); in a session the others keep the name the
// session started with, so a change is refused there (`coop.ui` / the window: before host / join)
int c_name(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && g_sess.role() != net::Role::None) {
        SAY("name: %s (cannot change it in a session; leave first)", g_name.c_str());
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "reload")) { // tests (BUG-0897): [coop] name of coop.ini, as at the start
        char nm[256];
        H->config_str(M, "coop", "name", "Player", nm, sizeof nm);
        g_name = utf8::player_name(nm);
        if (g_name.empty())
            g_name = "Player";
    } else if (argc > 1)
        g_name = utf8::player_name(argv[1]);
    SAY("name: %s", g_name.c_str());
    return 0;
}

int c_say(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string t;
    for (int i = 1; i < argc; i++) {
        if (i > 1) t += ' ';
        t += argv[i];
    }
    if (support::chat_command(t)) { // "/bug <text>": a mark in every player's log, not a chat line
        SAY("marked for the report");
        return 0;
    }
    if (!g_sess.connected()) {
        SAY("not connected");
        return 1;
    }
    g_sess.send_chat(t);
    SAY("sent");
    return 0;
}

int c_avatar(void*, int, const char* const*, SkOut* out)
{
    void* av = local_now(); // BUG-0271
    if (!av) {
        SAY("nothing controlled (mode 0x%08X %s, adapter %s)", game::mode_id(), modes::mode_name(game::mode_id()),
            g_adapter ? g_adapter->name : "none");
        return 1;
    }
    auto st = local_state();
    SAY("%s: local=%p place=0x%08X world '%s'\n", g_adapter->name, av, g_world, modes::current_world_name().c_str());
    SAY("species=%p key=0x%08X\npos=(%.2f %.2f %.2f) |r|=%.1f\nrot=(%.3f %.3f %.3f %.3f)\n"
        "vel=(%.2f %.2f %.2f)\nhp=%.1f/%.1f flags=0x%02X",
        g_adapter->mode == modes::kCreature || g_adapter->mode == modes::kScenario ? game::species_profile(av) : nullptr,
        st.species, st.pos.x, st.pos.y, st.pos.z,
        std::sqrt(st.pos.x * st.pos.x + st.pos.y * st.pos.y + st.pos.z * st.pos.z), st.rot.x, st.rot.y,
        st.rot.z, st.rot.w, st.vel.x, st.vel.y, st.vel.z, st.health, st.max_health, st.flags);
    if (g_adapter->mode != modes::kSpace && g_adapter->mode != modes::kCell) {
        float g = game::ground_radius(st.pos); // tests: the avatar's height above the terrain
        float r = std::sqrt(st.pos.x * st.pos.x + st.pos.y * st.pos.y + st.pos.z * st.pos.z);
        if (g > 0)
            SAY("\nground=%.2f above=%.2f", g, r - g);
    }

    if (g_adapter->mode == modes::kScenario) // tests (BUG-1254): the captain's size
        SAY(" scale=%.3f", game::scale(av));
    if (g_adapter->mode == modes::kCreature) // age +0xB34 (0 hatchling, 1 adult), the hatchling waiting for its family
        SAY(" age=%d hatchling_waiting=%d", *(int*)((uint8_t*)av + 0xB34), (int)game::hatchling_waiting(av));
    SAY("\ngame_name '%s'", modes::raw_world_name().c_str()); // BUG-0258: CurrentGameName as is
    return 0;
}

int c_echo(void*, int argc, const char* const* argv, SkOut* out)
{
    g_echo = argc < 2 || strcmp(argv[1], "off") != 0;
    if (argc > 2)
        g_echo_offset = (float)atof(argv[2]);
    if (argc > 3)
        g_echo_delay = atof(argv[3]);
    if (!g_echo) {
        auto it = g_remotes.find(200);
        if (it != g_remotes.end()) {
            despawn(it->second);
            g_remotes.erase(it);
        }
        g_echo_q.clear();
    }
    SAY("echo %s offset=%.1f delay=%.2f", g_echo ? "on" : "off", g_echo_offset, g_echo_delay);
    return 0;
}

int c_res(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc >= 3 && !strcmp(argv[1], "fix")) {
        SAY("%s", shared::fix(argv[2], argc >= 4 ? atoi(argv[3]) : -1).c_str());
        return 0;
    }
    if (argc >= 3 && (!strcmp(argv[1], "block") || !strcmp(argv[1], "away"))) { // tests: BUG-0691 / BUG-0692
        SAY("%s", shared::test_knob(argv[1], atof(argv[2]), argc >= 5 ? argv[3] : "", argc >= 5 ? atof(argv[4]) : 0).c_str());
        return 0;
    }
    // joinsim (tests, BUG-0220): what a join of another player runs, then the change, in one step
    bool joinsim = argc >= 5 && !strcmp(argv[1], "joinsim");
    if (joinsim)
        shared::on_player_join(g_sess.role());
    if ((argc >= 4 && !strcmp(argv[1], "add")) || (joinsim && !strcmp(argv[2], "add"))) {
        std::string err;
        if (!shared::debug_add(argv[2 + joinsim], atof(argv[3 + joinsim]), err)) {
            SAY("%s", err.c_str());
            return 1;
        }
    }
    SAY("%s", shared::status().c_str());
    return 0;
}

// coop.ping: mark the place under the mouse (RTS stages) or where you stand, for everybody
int c_ping(void*, int, const char* const*, SkOut* out)
{
    if (!g_local || g_sess.role() == net::Role::None) {
        SAY("play and join a session first");
        return 1;
    }
    net::PlayerState st = local_state();
    net::Writer w;
    w.u8(g_sess.my_id());
    w.f32(st.pos.x); w.f32(st.pos.y); w.f32(st.pos.z); w.u32(g_mode);
    if (g_sess.role() == net::Role::Host)
        g_sess.broadcast_reliable(net::MSG_PING_MARK, w);
    else
        g_sess.send_reliable_to(0, net::MSG_PING_MARK, w);
    add_mark(g_sess.my_id(), st.pos, g_mode);
    SAY("pinged (%.1f %.1f %.1f)", st.pos.x, st.pos.y, st.pos.z);
    return 0;
}

int c_share(void*, int argc, const char* const* argv, SkOut* out)
{
    if (g_sess.role() != net::Role::Host) {
        SAY("only the host shares its world");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "due")) { // tests (BUG-0404): a share due as after an evolution (the gate path)
        int k = 0;
        for (auto& [id, r] : g_remotes)
            if (!r.echo) {
                r.needs_world = true;
                r.cine_wait_since = 0;
                k++;
            }
        g_share_due_at = H->time_seconds();
        SAY("share due for %d player(s), the game is busy: %s", k, busy::names(game_busy()).c_str());
        return 0;
    }
    int n = 0;
    for (auto& p : g_sess.players()) {
        if (p.id == 0 || (argc > 1 && atoi(argv[1]) != p.id))
            continue;
        std::string err;
        if (world::share_with(p.id, err))
            n++;
        else
            SAY("player %u: %s\n", p.id, err.c_str());
    }
    SAY("sharing the world with %d player(s)", n);
    return 0;
}

int c_npc(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "animquiet")) { // guest (tests, BUG-0464)
        npc::set_anim_quiet((float)atof(argv[2]));
        SAY("ok\n");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "joinapart")) { // tests (BUG-0989)
        SAY("%s\n", join_apart_status().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "anims")) { // BUG-0464: the behaviour animation relay's counters
        SAY("%s", npc::anims_status().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "cur")) { // BUG-0464 (tests): each NPC's current animation
        SAY("%s", npc::current_anims().c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "untune")) { // guest (tests, BUG-0462): proxy <id> as made in a herd of ours
        SAY("%s", npc::debug_untune((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "gamekill")) { // guest (tests, BUG-0850): the game destroys proxy <id>, then it goes
        SAY("%s", npc::debug_gamekill((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    // tests (BUG-0426 class, L1): reusehunt <hex address | p<proxy id> | puppet> [frames] [flags]: hunt for a new creature
    // at the address (npc.cpp ReuseHunt); reusehunt alone: its status
    // guest (tests, BUG-0805): handlerkill <id> damage|prompt [anim hex]: the game destroys proxy <id>'s creature
    // (DestroyInstance, no bookkeeping: the proxy still names it, as before the next npc tick), then in the same call a
    // host message about that proxy is handled: damage = the host says the guest was hit by it (MSG_DAMAGE),
    // prompt = its social prompt (MSG_SOCIAL K_PROMPT)
    if (argc > 3 && !strcmp(argv[1], "handlerkill")) {
        uint16_t id = (uint16_t)atoi(argv[2]);
        void* c = npc::debug_proxy_creature(id);
        if (!c || !game::creature_listed(c) || !g_sess.connected() || g_sess.role() != net::Role::Client) {
            SAY("no live proxy %u (a guest's)\n", id);
            return 1;
        }
        uint32_t d0 = npc::handler_dropped();
        game::destroy_untracked(c);
        net::Writer w;
        if (!strcmp(argv[3], "damage")) {
            net::Damage d;
            d.player = g_sess.my_id();
            d.damage = 0.1f;
            d.attacker = id;
            d.write(w);
            net::Reader r(w.data.data(), w.data.size());
            npc::on_message(net::MSG_DAMAGE, 0, r);
        } else {
            w.u8(5); // K_PROMPT
            w.u16(id);
            w.u8(0);
            w.u8(1);
            w.u32(0);
            w.u32(0);
            w.u32(argc > 4 ? (uint32_t)strtoul(argv[4], nullptr, 16) : 0x0F3A8D11u);
            net::Reader r(w.data.data(), w.data.size());
            social::on_message(net::MSG_SOCIAL, 0, r);
        }
        SAY("handlerkill: proxy %u's creature %p destroyed by the game, %s handled; calls dropped %u (BUG-0805)\n", id, c,
            argv[3], npc::handler_dropped() - d0);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "settle")) { // guest (tests, BUG-0842): settle <s>
        SAY("%s", npc::debug_settle(atof(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "chunk")) { // tests (BUG-1099): chunk p<proxy id> | <hex address>
        void* t = argv[2][0] == 'p' ? npc::debug_proxy_creature((uint16_t)atoi(argv[2] + 1))
                                    : (void*)strtoul(argv[2], nullptr, 16);
        SAY("%s", npc::debug_chunk(t).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "reusehunt")) {
        if (argc < 3) {
            SAY("%s", npc::debug_reuse_status().c_str());
            return 0;
        }
        void* t = nullptr;
        if (argv[2][0] == 'p' && argv[2][1] != 'u')
            t = npc::debug_proxy_creature((uint16_t)atoi(argv[2] + 1));
        else if (!strcmp(argv[2], "puppet")) {
            for (auto& [id, r] : g_remotes)
                if (r.puppet && creature_puppet(r.puppet_adapter))
                    t = r.puppet;
        } else
            t = (void*)strtoul(argv[2], nullptr, 16);
        if (!t) {
            SAY("no such object");
            return 1;
        }
        const char* fl = argc > 4 ? argv[4] : "";
        if (strchr(fl, 'H')) { // H: t is a herd; the tries are herds of the avatar's species (BUG-0843)
            bool hl = game::listed(t, game::kHerdNoun);
            net::Vec3 hat = game::avatar() ? game::position(game::avatar()) : net::Vec3{};
            if (fl[0] == 'x' && hl)
                game::destroy_untracked(t);
            SAY("%s%s", fl[0] == 'x' && hl ? "herd destroyed by the game; " : hl ? "" : "(not a listed herd) ",
                npc::debug_reuse_hunt(t, argc > 3 ? atoi(argv[3]) : 120, nullptr, 0, 1, nullptr, &hat, true).c_str());
            return 0;
        }
        bool listed = game::creature_listed(t);
        void* sp = listed && !strchr(fl, 'a') ? game::species_profile(t) : nullptr; // a: of the avatar's species
        void* herd = listed && strchr(fl, 'h') ? game::herd_of(t) : nullptr; // h: in the herd the target was in
        net::Vec3 at = listed ? game::position(t) : net::Vec3{};
        // x: the game destroys it first (DestroyInstance, no bookkeeping of ours); n: the first tries run now, in this
        // call (no frame of ours between); b<N>: N creatures per tick (the first tick after the destroy is in the frame
        // the game frees it in)
        bool x = fl[0] == 'x';
        if (x && listed)
            game::destroy_untracked(t);
        int now_tries = strchr(fl, 'n') ? (argc > 5 ? atoi(argv[5]) : 40) : 0;
        const char* bb = strchr(fl, 'b');
        int burst = bb ? std::max(1, atoi(bb + 1)) : 1;
        // k: the misses stay alive until the hunt ends (their blocks do not go back first); e<N>: a try every N frames
        // f<N>: once the target's heap chunk is free, at most N more tries (BUG-0825); w<N>: no try before it is free, the round ends
        // when it is not free after N frames (default 60, BUG-1099)
        const char* ee = strchr(fl, 'e');
        const char* ff = strchr(fl, 'f');
        SAY("%s%s", x ? "destroyed by the game; " : "",
            npc::debug_reuse_hunt(t, argc > 3 ? atoi(argv[3]) : 120, sp, now_tries, burst, herd, &at, false,
                                  strchr(fl, 'r') != nullptr, // r: Create with its reuse flag
                                  strchr(fl, 'k') != nullptr, ee ? atoi(ee + 1) : 1).c_str());
        if (const char* ww = strchr(fl, 'w'))
            npc::debug_reuse_free_first(atoi(ww + 1));
        if (ff)
            npc::debug_reuse_wait_free(atoi(ff + 1) > 0 ? atoi(ff + 1) : 20);
        // B<N>: N creatures a tick once the chunk is free; K: the misses kept from the free on; M: a target whose chunk
        // merges backward ends at once (BUG-1406)
        const char* bf = strchr(fl, 'B');
        if (bf || strchr(fl, 'K') || strchr(fl, 'M'))
            npc::debug_reuse_after_free(bf ? std::max(1, atoi(bf + 1)) : 0, strchr(fl, 'K') != nullptr, strchr(fl, 'M') != nullptr);
        SAY("%s", npc::debug_reuse_status().c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "forget")) { // guest (tests, BUG-0487): forget <id>
        SAY("%s", npc::debug_forget((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "resync")) {
        SAY("%s", npc::resync_status().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "list")) {
        SAY("%s", npc::status().c_str());
        // out_printf keeps 8 KB per call: a guest's list with 40 proxies is longer (BUG-0305)
        std::string l = npc::list();
        for (size_t at = 0; at < l.size();) {
            size_t end = at + 6000 < l.size() ? l.rfind('\n', at + 6000) : l.size() - 1;
            if (end == std::string::npos || end < at)
                end = std::min(l.size(), at + 6000) - 1;
            SAY("%s", l.substr(at, end + 1 - at).c_str());
            at = end + 1;
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "marker") && !strcmp(argv[2], "unplace")) { // tests (BUG-1457)
        modes::test_unplace_marker();
        SAY("pointer unplaced (0 0 0), the last mouse move forgotten");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "marker")) { // tests (BUG-0280): pin the Tribe / Civ pointer (x y z | off)
        bool pin = argc > 4;
        net::Vec3 p{};
        if (pin)
            p = {(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        modes::test_pin_marker(pin, p);
        SAY("pointer %s (%.1f %.1f %.1f)", pin ? "pinned" : "follows the mouse", p.x, p.y, p.z);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "herdnulls")) { // tests (BUG-0256)
        SAY("%s", game::herd_nulls_status().c_str());
        return 0;
    }
    if (argc > 5 && !strcmp(argv[1], "dash")) { // host (tests, BUG-0305): dash <id> <m> <m/s> <s> [w] [mate id] [m beside]
        SAY("%s", npc::debug_dash((uint16_t)atoi(argv[2]), (float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]),
                                  argc > 6 ? (float)atof(argv[6]) : 0.f, argc > 7 ? (uint16_t)atoi(argv[7]) : 0,
                                  argc > 8 ? (float)atof(argv[8]) : 0.f).c_str());
        return 0;
    }
    if (argc > 7 && !strcmp(argv[1], "dashto")) { // host (tests, BUG-0440): dashto <id> <x> <y> <z> <m/s> <s> [vel factor]
        net::Vec3 to{(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])};
        SAY("%s", npc::debug_dash((uint16_t)atoi(argv[2]), 0.f, (float)atof(argv[6]), (float)atof(argv[7]), 0.f, 0, 0.f, &to,
                                  argc > 8 ? (float)atof(argv[8]) : 1.f).c_str());
        return 0;
    }
    if (argc > 5 && !strcmp(argv[1], "objnear")) { // tests (BUG-0440): objnear <x> <y> <z> <r>: the objects (nouns) within r m
        net::Vec3 p{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        SAY("%s", game::objects_near(p, (float)atof(argv[5]), nullptr, 40).c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "bhv")) { // host (tests, BUG-0440): bhv <id> <anim hex>
        SAY("%s", npc::debug_bhv((uint16_t)atoi(argv[2]), (uint32_t)strtoul(argv[3], nullptr, 16)).c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "pushback")) { // guest (tests, BUG-0440): pushback <id> <m/s> <s>
        SAY("%s", npc::debug_pushback((uint16_t)atoi(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])).c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "replace")) { // host (tests, BUG-1388): replace <id> <dist_m>
        SAY("%s", npc::debug_replace((uint16_t)atoi(argv[2]), (float)atof(argv[3])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "raised")) { // tests (BUG-0440): raised <r> [x y z]
        net::Vec3 at{argc > 5 ? (float)atof(argv[3]) : 0.f, argc > 5 ? (float)atof(argv[4]) : 0.f, argc > 5 ? (float)atof(argv[5]) : 0.f};
        SAY("%s", npc::debug_raised((float)atof(argv[2]), argc > 5 ? &at : nullptr).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "nanvel")) { // host (tests, BUG-0894): nanvel <s> [id|-1] [air]
        SAY("%s", npc::debug_nanvel((float)atof(argv[2]), argc > 3 ? atoi(argv[3]) : -1, argc > 4 && !strcmp(argv[4], "air")).c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "rxkeep")) { // guest (tests, BUG-0611): rxkeep <n> <s>
        SAY("%s", npc::debug_rxkeep(atoi(argv[2]), (float)atof(argv[3])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "pool")) { // host (tests, BUG-0339): pool <id>
        SAY("%s", npc::debug_pool((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "lift")) { // host (tests, BUG-0550): lift <id> <m> <s>
        SAY("%s", npc::debug_lift((uint16_t)atoi(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])).c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "sink")) { // guest (tests, BUG-0399): sink <id> <m> [mark]
        SAY("%s", npc::debug_sink((uint16_t)atoi(argv[2]), (float)atof(argv[3]), argc > 4 && atoi(argv[4]) != 0).c_str());
        return 0;
    }
    if (argc > 7 && !strcmp(argv[1], "pwalk")) { // guest (tests, BUG-0281): pwalk <id> x y z <m/s> <s> [mode bits]
        SAY("%s", npc::debug_pwalk((uint16_t)atoi(argv[2]), {(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])},
                                   (float)atof(argv[6]), (float)atof(argv[7]), argc > 8 ? atoi(argv[8]) : 0).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "herds")) {
        SAY("%s", game::herds_dump().c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "gopos")) { // tests: the avatar to a point (x y z)
        void* av = game::avatar();
        if (!av) {
            SAY("no avatar");
            return 1;
        }
        // BUG-0400: tests shift a partner's position along a world axis (team: host x + 1.5 m), which is
        // a chord of the planet: up to 0.5 m nearer its centre, often under the terrain, and the game
        // does not lift a grounded creature placed under it (docs/re/creature.md#creature-puppet-ground).
        // A point under the ground goes onto it (`coop.fix tools.gopos_ground off`: as given, 0.7.3.3).
        net::Vec3 p{(float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])};
        float r = drive::speed(p), g = game::ground_radius(p);
        bool lifted = g_gopos_ground_fix && g > 1 && r > 1 && r < g;
        if (lifted)
            p = {p.x * g / r, p.y * g / r, p.z * g / r};
        float up = gopos_onto_surface(p);
        game::teleport(av, p, game::orientation(av));
        if (up > 0.f)
            SAY("avatar moved (onto the surface: %.2f m up, a mound or a rock)", up);
        else
            SAY("avatar moved%s", lifted ? " (onto the ground)" : "");
        return 0;
    }
    if (argc > 2 && (!strcmp(argv[1], "goherd") || !strcmp(argv[1], "gonpc"))) { // tests: the avatar d m
                                                                                  // from a herd's home / host NPC
        void* h = (void*)(uintptr_t)strtoul(argv[2], nullptr, 16);
        void* av = game::avatar();
        void* npc = argv[1][2] == 'n' ? npc::host_npc((uint16_t)atoi(argv[2])) : nullptr; // gonpc <id>
        if (!av || (!npc && (!h || IsBadReadPtr(h, 0x40) || game::noun_id(h) != game::kHerdNoun))) {
            SAY("no avatar or not a herd / host npc");
            return 1;
        }
        net::Vec3 p;
        if (npc)
            p = game::position(npc);
        else {
            auto* home = (float*)((uint8_t*)h + 0x34);
            p = {home[0], home[1], home[2]};
        }
        float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), d = argc > 3 ? (float)atof(argv[3]) : 12.f;
        net::Vec3 up = r > 1 ? net::Vec3{p.x / r, p.y / r, p.z / r} : net::Vec3{0, 0, 1};
        // d metres to the side (any direction perpendicular to up) and a little above the ground
        net::Vec3 side = std::fabs(up.x) < 0.9f ? net::Vec3{0, -up.z, up.y} : net::Vec3{-up.z, 0, up.x};
        float sl = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
        net::Vec3 q{p.x + side.x / sl * d + up.x * 2, p.y + side.y / sl * d + up.y * 2, p.z + side.z / sl * d + up.z * 2};
        // BUG-0400 as for gopos: on a slope the point d m aside can be under the terrain (pack_target: the host's
        // own creature 0.6-0.7 m under it for 10 s+, the monitor's ground rule), so it goes onto the ground
        float qr = drive::speed(q), qg = game::ground_radius(q);
        if (g_gopos_ground_fix && qg > 1 && qr > 1 && qr < qg + 0.3f)
            q = {q.x * (qg + 0.3f) / qr, q.y * (qg + 0.3f) / qr, q.z * (qg + 0.3f) / qr};
        game::teleport(av, q, game::orientation(av));
        SAY("avatar at (%.1f %.1f %.1f)", q.x, q.y, q.z);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "epic")) { // host (tests): epic <player id, 0 = host> [dist] [archetype hex] [species hex]
        void* c = npc::debug_spawn_epic((uint8_t)atoi(argv[2]), argc > 3 ? (float)atof(argv[3]) : 8.f,
                                        argc > 4 ? (uint32_t)strtoul(argv[4], nullptr, 16) : 0x95511253u,
                                        argc > 5 ? (uint32_t)strtoul(argv[5], nullptr, 16) : 0u);
        SAY(c ? "epic %p" : "no epic (not the host, no such player or no NPC species near)", c);
        return c ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "partners")) { // tests: NPCs target partners' puppets on/off
        npc::set_partner_targets(strcmp(argv[2], "off") != 0);
        SAY("npcs target partners %s", strcmp(argv[2], "off") ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "cinedefer")) { // tests: off = remove creatures at once (before 0.7.2.4)
        game::set_destroy_defer(strcmp(argv[2], "off") != 0);
        SAY("creatures removed during a cinematic: %s", strcmp(argv[2], "off") ? "after it" : "at once");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "strays")) { // tests
        npc::set_strays(strcmp(argv[2], "off") != 0);
        SAY("stray removal %s", strcmp(argv[2], "off") ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "killfix")) { // tests: off = the guest's kill list survives skipped frames (BUG-0082)
        npc::set_kill_clear(strcmp(argv[2], "off") != 0);
        SAY("kill list cleared every frame: %s", strcmp(argv[2], "off") ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "kill")) { // host (tests, BUG-1100): kill <id>: the NPC's object marked dead
        SAY("%s", npc::debug_kill((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "reuse")) { // host (tests): reuse <id>: a dead NPC's object alive again (BUG-0044)
        SAY("%s", npc::debug_reuse((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && (!strcmp(argv[1], "reusefix") || !strcmp(argv[1], "idfix"))) { // tests (BUG-0044 / BUG-0023)
        bool on = strcmp(argv[2], "off") != 0;
        (argv[1][0] == 'r' ? npc::set_reuse_fix : npc::set_id_fix)(on);
        SAY("%s %s", argv[1], on ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "swapguard")) { // tests: off = no undo of a swap left open (BUG-0110)
        game::set_swap_guard(strcmp(argv[2], "off") != 0);
        SAY("swap guard %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "sharecine")) { // tests: off = the world goes during a cinematic (BUG-0019)
        g_share_cine_fix = strcmp(argv[2], "off") != 0;
        SAY("sharecine %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "deathfix")) { // tests: off = a killed proxy stays alive at 0 health (BUG-0015, BUG-0148)
        npc::set_death_fix(strcmp(argv[2], "off") != 0);
        SAY("deathfix %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "stallfix")) { // tests: off = the old proxy driving (BUG-0169)
        npc::set_stall_fix(strcmp(argv[2], "off") != 0);
        SAY("stallfix %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "knownfix")) { // tests: off = a leaving guest's known NPCs stay (BUG-0125)
        npc::set_known_fix(strcmp(argv[2], "off") != 0);
        SAY("knownfix %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "worldfix")) { // tests: off = worlds told apart by the avatar address only (BUG-0119)
        game::set_world_epoch_fix(strcmp(argv[2], "off") != 0);
        SAY("world counter %s (now %u)", argv[2], game::world_epoch());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "swapfault")) { // tests: the next swapped NPC tick skips its restore
        if (argc > 2 && !strcmp(argv[2], "ledger")) { // the other detours (BUG-0110)
            game::debug_swap_ledger_fault();
            SAY("the next detour that changes game state around a call leaves like a fault");
            return 0;
        }
        game::debug_swap_fault();
        SAY("the next NPC tick with a partner as the player leaves like a fault");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "nextid")) { // host (tests): the next net id tried (BUG-0023)
        npc::debug_next_id((uint16_t)atoi(argv[2]));
        SAY("next npc id %s", argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "killhold")) { // tests: the avatar into the guest's kill list
        npc::debug_kill_hold();
        SAY("held");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "hatch")) { // tests
        npc::set_hatch_help(strcmp(argv[2], "off") != 0);
        SAY("hatch help %s", strcmp(argv[2], "off") ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "attackmsg")) { // tests: host avatar "attacks" host NPC #id
        bool ok = npc::debug_attack_msg((uint16_t)atoi(argv[2]));
        SAY(ok ? "attack message sent" : "no such npc");
        return ok ? 0 : 1;
    }
    if (argc > 1 && !strcmp(argv[1], "archless")) { // tests: herds like ours (no archetype)
        SAY("%s", npc::herds(argc > 2 && !strcmp(argv[2], "all")).c_str());
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "untuned") || !strcmp(argv[1], "creatures"))) { // tests
        SAY("%s", npc::creatures(argv[1][0] == 'u', argc > 2 ? strtoul(argv[2], nullptr, 16) : 0).c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "hurt")) {
        bool ok = npc::debug_hurt((uint8_t)atoi(argv[2]), (uint16_t)atoi(argv[3]), (float)atof(argv[4]));
        SAY(ok ? "puppet hit through the damage filter" : "no such npc/puppet");
        return ok ? 0 : 1;
    }
    if (argc > 5 && !strcmp(argv[1], "place")) { // host (tests, BUG-0815): NPC #id put at x y z (the game's teleport)
        void* c = npc::host_npc((uint16_t)atoi(argv[2]));
        if (c)
            game::teleport(c, {(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])}, game::orientation(c));
        SAY(c ? "npc placed" : "no such npc");
        return c ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "hosthit")) { // host avatar hits host NPC #id
        bool ok = npc::debug_host_hit((uint16_t)atoi(argv[2]), (float)atof(argv[3]), argc > 4 ? (float)atof(argv[4]) : 0.f);
        SAY(ok ? "host hit applied" : "no such npc");
        return ok ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "spawn")) { // host: an NPC of crt species <hex> next to the avatar
        void* c = npc::debug_spawn((uint32_t)strtoul(argv[2], nullptr, 16));
        SAY(c ? "spawned %p" : "unknown species or no avatar", c);
        return c ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "ghost")) { // guest, tests (BUG-0931): proxy #id names memory that is no creature
        SAY("%s", npc::debug_ghost((uint16_t)atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "recruit")) { // host: NPC #id joins the pack (AddMember)
        bool ok = npc::debug_recruit((uint16_t)atoi(argv[2]));
        SAY(ok ? "joined the pack" : "no such npc or it cannot join");
        return ok ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "gone")) { // host, tests (BUG-1428): gone <id> [secs]
        SAY("%s", npc::debug_gone((uint16_t)atoi(argv[2]), argc > 3 ? (float)atof(argv[3]) : 3.f).c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "hit")) {
        bool ok = npc::debug_hit((uint16_t)atoi(argv[2]), (float)atof(argv[3]));
        SAY(ok ? "hit sent through the damage filter" : "no such spawned proxy (a guest) or NPC (the host)");
        return ok ? 0 : 1;
    }
    if (argc > 1)
        npc::set_enabled(strcmp(argv[1], "off") != 0);
    SAY("npc sync %s\n%s", npc::enabled() ? "on" : "off", npc::status().c_str());
    return 0;
}

// BUG-0422: coop.players held <id>: the per-player state the modules still keep for a player id (player_life.h)
static int c_players(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "held")) {
        std::string h = player_life::held((uint8_t)atoi(argv[2]));
        SAY("%s", h.empty() ? "nothing held\n" : h.c_str());
        return 0;
    }
    SAY("modules with per-player state: %zu\n", player_life::table().size());
    return 0;
}

// BUG-1055: coop.lifecycle [held]: the session / world state the modules registered (lifecycle.h); `held` prints what
// is held now and what the last reset of each scope left ("left session: none" is what a test wants)
static int c_lifecycle(void*, int argc, const char* const* argv, SkOut* out)
{
    auto lines = [](const std::string& s) {
        std::string o;
        for (char c : s)
            o += c == '\n' ? ',' : c;
        if (!o.empty() && o.back() == ',')
            o.pop_back();
        return o.empty() ? std::string("none") : o;
    };
    if (argc > 1 && !strcmp(argv[1], "held")) {
        SAY("now: %s\n", lines(lifecycle::held()).c_str());
        SAY("left session: %s (resets %u)\n", lines(lifecycle::left(lifecycle::kSession)).c_str(),
            lifecycle::resets(lifecycle::kSession));
        SAY("left world: %s (resets %u)\n", lines(lifecycle::left(lifecycle::kWorld)).c_str(),
            lifecycle::resets(lifecycle::kWorld));
        return 0;
    }
    // C2 (BUG-1141): `world`: the world identity now; `hooks`: the observers with their calls and faults
    if (argc > 1 && !strcmp(argv[1], "world")) {
        SAY("%s", world_id_status().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "hooks")) {
        auto& f = lifecycle::fanout();
        SAY("hooks: %zu fired session=%u world=%u join=%u leave=%u faults=%u (last %s) nested=%u queued=%u\n",
            f.hooks.size(), f.fired[0], f.fired[1], f.fired[2], f.fired[3], f.faults,
            f.last_fault.empty() ? "-" : f.last_fault.c_str(), f.nested, f.queued);
        SAY("%s", lifecycle::hooks_status().c_str());
        return 0;
    }
    SAY("lifecycle: fix=%d entries=%zu hooks=%zu\n", (int)g_lifecycle_fix, lifecycle::table().size(),
        lifecycle::fanout().hooks.size());
    for (auto& e : lifecycle::table())
        SAY("  %s %s%s%s held=%zu\n", e.name, (e.scope & lifecycle::kSession) ? "session" : "",
            e.scope == (lifecycle::kSession | lifecycle::kWorld) ? "+" : "", (e.scope & lifecycle::kWorld) ? "world" : "",
            e.held());
    for (uint8_t ev : {lifecycle::kSessionEnd, lifecycle::kWorldChange}) // the observers in their order (no counts)
        for (size_t i : lifecycle::hooks_of(ev)) {
            const auto& h = lifecycle::fanout().hooks[i];
            SAY("  hook %s %d %s%s\n", ev == lifecycle::kSessionEnd ? "session" : "world", h.order, h.name,
                h.needs_game ? " game" : "");
        }
    return 0;
}

// D2 (docs/plan-0.7.15.md, audit TST-4): coop.testreset [fp | plant <n>]: the lab's warm reset contract. Without an
// argument every registered session and world state is reset (lifecycle::reset_all: the resets a session end and a
// world change run, mod state only; no hooks, no game calls), then the fingerprint is printed: what still holds
// something, one "  <name> <digest>" line each. A fresh game prints none; twoinst.py compares a warm game's with its
// cold start's and starts the lane cold when they differ (docs/06-testing.md#testing-warm-fingerprint). `fp`: the
// fingerprint only, nothing reset. `plant <n>`: tests (the lab's self-test of the fingerprint): state of a module
// whose reset forgets it (test.planted_leftover), as the absolute counters of BUG-0356 / BUG-0396 were.
static uint32_t g_planted_leftover;
SESSION_STATE("test.planted_leftover", nullptr, [] { /* forgets it on purpose (the planted leftover) */ },
              []() -> size_t { return g_planted_leftover; });
static int c_testreset(void*, int argc, const char* const* argv, SkOut* out)
{
    bool reset = true;
    if (argc > 2 && !strcmp(argv[1], "plant")) {
        g_planted_leftover = (uint32_t)strtoul(argv[2], nullptr, 0);
        SAY("planted leftover: test.planted_leftover %u (no reset clears it)\n", g_planted_leftover);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "fp"))
        reset = false;
    if (reset)
        lifecycle::reset_all(g_lifecycle_fix);
    lifecycle::Fingerprint f = lifecycle::fingerprint();
    SAY("testreset: reset=%d fix=%d entries=%zu holding=%zu hash=%08X\n", (int)reset, (int)g_lifecycle_fix, f.entries,
        f.holding, f.hash);
    size_t a = 0;
    while (a < f.lines.size()) {
        size_t b = f.lines.find('\n', a);
        SAY("  %s\n", f.lines.substr(a, b - a).c_str());
        a = b + 1;
    }
    return 0;
}

// BUG-1047: coop.guard [unwind <name> [n] | stick <name>]: the modules' call guards (call_guard.h). `unwind`: the next
// n holds of that guard end as a fault the game caught ends them (the guard stays set); `stick`: set it now. The frame's
// swap guard tick clears a guard left set (`coop.fix core.call_guard_reset off`: it stays set).
static int c_guard(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "unwind")) {
        bool ok = callguard::unwind(argv[2], argc > 3 ? (uint32_t)atoi(argv[3]) : 1u);
        SAY(ok ? "unwind %s armed\n" : "no guard %s\n", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "stick")) {
        callguard::Entry* e = callguard::find(argv[2]);
        if (!e) {
            SAY("no guard %s\n", argv[2]);
            return 0;
        }
        if (e->flag)
            *e->flag = true;
        else
            *e->depth = 1;
        SAY("%s set\n", argv[2]);
        return 0;
    }
    SAY("guards: entries=%zu cleared=%u last=%s\n", callguard::table().size(), callguard::state().cleared,
        callguard::state().last.empty() ? "none" : callguard::state().last.c_str());
    SAY("%s", callguard::status().c_str());
    return 0;
}

int c_social(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "keys")) { // tests: whether the ability keys are taken now
        SAY("text_up=%d menu_up=%d", (int)g_text_up.load(), (int)g_menu_up.load());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "solo")) { // tests (BUG-1527)
        SAY("%s", social::solo_dump().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "species")) {
        SAY("%s", social::species_dump().c_str());
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "answer")) { // tests (BUG-0525): answer mirror <id> with the ability of type <hex>
        bool ok = social::debug_answer((uint16_t)atoi(argv[2]), strtoul(argv[3], nullptr, 16));
        SAY(ok ? "answer sent" : "no answer (not a guest, no such proxy or ability)");
        return ok ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "abil")) { // tests (BUG-0525): abilities of NPC <id> (host) / proxy (guest), -1 the avatar
        SAY("%s", social::abilities_dump(atoi(argv[2])).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "bar")) { // tests (BUG-1242): the HUD bar's slots with their abilities' kinds
        SAY("%s", social::bar_dump().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "herds")) { // tests (BUG-1242): nest herds, their species and prompt kinds
        SAY("%s", social::herds_dump().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "games")) { // tests (BUG-0831): the host's social games and their prompts
        SAY("%s", social::games_dump().c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "meter_null")) { // tests (BUG-1058): the next n meter tasks find no meter window
        social::debug_meter_null((unsigned)atoi(argv[2]));
        SAY("the next %s meter tasks find no meter window\n", argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "meter")) { // tests (BUG-0179): the game's social meter on this screen
        SAY("%s", social::meter_dump().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "rel")) {
        SAY("%s", social::relations_dump().c_str());
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "goal")) { // tests: goal <hex crt> <outcome 5|6> <dna> (guest -> host, BUG-0205)
        bool ok = social::debug_send_goal(strtoul(argv[2], nullptr, 16), atoi(argv[3]), (float)atof(argv[4]));
        SAY(ok ? "goal sent" : "not a connected guest");
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "record")) { // tests: record <hex crt> <relation> <impressed>
        bool ok = social::debug_set_record(strtoul(argv[2], nullptr, 16), atoi(argv[3]), (uint32_t)atoi(argv[4]));
        SAY(ok ? "record set" : "no relationship manager");
        return ok ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "setdefault")) { // tests (BUG-1540): setdefault <npc id> <rel>
        int old = social::debug_set_default((uint16_t)atoi(argv[2]), atoi(argv[3]));
        if (old < 0) {
            SAY("no such NPC or tuning");
            return 1;
        }
        SAY("default %d -> %d", old, atoi(argv[3]));
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "event")) { // tests (BUG-0168): event <hex crt> <n>, the game's ApplyEvent
        bool ok = social::debug_event(strtoul(argv[2], nullptr, 16), atoi(argv[3]));
        SAY(ok ? "event applied" : "no relationship manager or event out of 0..7");
        return ok ? 0 : 1;
    }
    if (argc > 2 && !strcmp(argv[1], "goto")) {
        bool ok = social::debug_goto((uint16_t)atoi(argv[2]));
        SAY(ok ? "next to it" : "no such spawned proxy");
        return ok ? 0 : 1;
    }
    if (argc > 3 && !strcmp(argv[1], "objs")) {
        SAY("%s", social::debug_objects(strtoul(argv[2], nullptr, 16), strtoul(argv[3], nullptr, 16)).c_str());
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "post")) { // tests (BUG-0994): post <hex creature>: the avatar's social click on it
        SAY("%s", social::debug_post((void*)strtoul(argv[2], nullptr, 16)).c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "log")) {
        social::debug_social(0, -1);
        SAY("toggled");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "act")) {
        bool ok = social::debug_social((uint16_t)atoi(argv[2]), argc > 3 ? atoi(argv[3]) : 0);
        SAY(ok ? "social action sent" : "no action (no such proxy, too far, no social ability or cooling down)");
        return ok ? 0 : 1;
    }
    SAY("%s", social::status().c_str());
    return 0;
}

int c_overlay(void*, int argc, const char* const* argv, SkOut* out)
{
    // tests (BUG-0796): animrx <player id> <hex anim> [n]: as if player id's MSG_ANIM arrived n times (default 1), whatever
    // stage this game shows that player in (a cell in Cell, a marker in Tribe)
    if (argc > 3 && !strcmp(argv[1], "animrx")) {
        net::AnimEvent ev;
        ev.id = (uint8_t)atoi(argv[2]);
        ev.anim = (uint32_t)strtoul(argv[3], nullptr, 16);
        int n = argc > 4 ? std::max(1, atoi(argv[4])) : 1;
        uint32_t played0 = g_anims_played;
        for (int i = 0; i < n; i++)
            on_remote_anim(ev);
        auto it = g_remotes.find(ev.id);
        SAY("animrx: %d delivered to player %u (puppet %p, %s): played=%u not_creature=%u\n", n, ev.id,
            it != g_remotes.end() ? it->second.puppet : nullptr,
            it != g_remotes.end() && it->second.puppet_adapter ? it->second.puppet_adapter->name : "-",
            g_anims_played - played0, g_anims_not_creature);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "animsend")) { // tests: as if the avatar played animation <hex> (BUG-0091)
        void* av = game::avatar();
        if (av)
            on_anim(av, (uint32_t)strtoul(argv[2], nullptr, 16), 0, false, false);
        SAY(av ? "animation sent" : "no avatar");
        return av ? 0 : 1;
    }
    if (argc > 1 && !strcmp(argv[1], "anims")) {
        g_anim_log = !g_anim_log;
        SAY("avatar animation log %s", g_anim_log ? "on" : "off");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "self")) {
        g_tag_self = !g_tag_self;
        SAY("self tag %s", g_tag_self ? "on" : "off");
        return 0;
    }
    g_overlay = argc < 2 || strcmp(argv[1], "off") != 0;
    SAY("overlay %s", g_overlay ? "on" : "off");
    return 0;
}

int c_team(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "point")) { // tests: where the first partner is on the screen
        for (auto& [id, r] : g_remotes)
            if (puppet_now(r) && !r.echo) {
                net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
                float w[3] = {p.x, p.y, p.z}, sc[2] = {0, 0};
                int ok = H->world_to_screen(w, sc);
                uint8_t* cr = (uint8_t*)r.puppet;
                SAY("point %d %d %d flags=%08X e84=%p guards=%u msg_guards=%u msg_other=%u", ok, (int)sc[0], (int)sc[1],
                    g_mode == modes::kCreature ? *(uint32_t*)(cr + 0xB58) : 0u,
                    g_mode == modes::kCreature ? *(void**)(cr + 0xE84) : nullptr, g_select_guards, g_stage_msg_guards,
                    g_stage_msg_other);
                return 0;
            }
        SAY("no partner");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "die") && local_now() && g_mode == modes::kCreature) { // tests: a lethal hit
        game::apply_damage(local_now(), 1e6f, 0, nullptr); // the game's damage path (death, caption, respawn)
        SAY("lethal hit applied");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "hit") && local_now() && g_mode == modes::kCreature) { // tests: a hit through
        game::hit(local_now(), (float)atof(argv[2]), 0, nullptr); // the damage filter (the pack rescue applies)
        SAY("hit applied, hp %.1f", game::health(local_now()));
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "pvp") && local_now() && g_mode == modes::kCreature) { // tests (F3 friendly fire):
        auto it = g_remotes.find((uint8_t)atoi(argv[2]));                                  // our creature hits a partner's
        if (it == g_remotes.end() || !puppet_now(it->second)) {                             // puppet through the game's
            SAY("no puppet of player %s", argv[2]);                                         // TakeDamage (the filter)
            return 1;
        }
        game::hit(it->second.puppet, (float)atof(argv[3]), 0, local_now());
        SAY("hit on player %s's creature sent through the damage filter", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "hp") && local_now() && g_mode == modes::kCreature) { // tests
        game::set_health(local_now(), (float)atof(argv[2]));
        SAY("hp set");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "come")) { // tests: our creature next to the first partner
        for (auto& [id, r] : g_remotes)
            if (puppet_now(r) && local_now() && g_mode == modes::kCreature) {
                net::Vec3 p = modes::entity_pos(r.puppet_adapter, r.puppet);
                net::Vec3 me = modes::entity_pos(g_adapter, local_now());
                float d = net::dist(p, me);
                // 3 m from the partner, on our side of it
                net::Vec3 at = d > 0.1f ? net::Vec3{p.x + (me.x - p.x) * 3.f / d, p.y + (me.y - p.y) * 3.f / d,
                                                    p.z + (me.z - p.z) * 3.f / d}
                                        : p;
                game::teleport(local_now(), at, net::Quat{0, 0, 0, 1});
                SAY("moved next to %s (%.1f m away before)", r.name.c_str(), d);
                return 0;
            }
        SAY("no partner");
        return 1;
    }
    if (argc > 1)
        teamplay::set_enabled(strcmp(argv[1], "off") != 0);
    SAY("teamplay %s\n%s", teamplay::enabled() ? "on" : "off", teamplay::status().c_str());
    return 0;
}

int c_uistate(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1 && !strcmp(argv[1], "log")) {
        coop_ui::g_log_msgs = !coop_ui::g_log_msgs;
        SAY("ui message log %s", coop_ui::g_log_msgs ? "on" : "off");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "lang")) { // tests: the layout of the texts that change in Russian
        coop_ui::set_russian(!strcmp(argv[2], "ru"));
        SAY("ui lang %s (the texts set from now on)", coop_ui::russian() ? "ru" : "en");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "reset")) { // tests (BUG-1139): closed, on the lobby page, as at a cold start
        coop_ui::test_reset();
        SAY("%s", coop_ui::debug_state().c_str());
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "first")) { // tests: the window as at the first start (the name step)
        coop_ui::first_start();
        SAY("ui: back to the first start (the name step)");
        return 0;
    }
    if (argc > 2 && (!strcmp(argv[1], "nudge") || !strcmp(argv[1], "hide"))) { // tests: ui_golden's twin
        bool hide = !strcmp(argv[1], "hide");
        SAY("%s", coop_ui::test_nudge(argv[2], argc > 3 && !hide ? (float)atof(argv[3]) : 0.f,
                                      argc > 4 && !hide ? (float)atof(argv[4]) : 0.f, hide,
                                      argc > 5 && !hide ? (float)atof(argv[5]) : 0.f).c_str()); // [dw]: BUG-1065's twin
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "fullscreen")) { // tests (BUG-1528): the report's full-screen answer
        coop_ui::set_test_fullscreen(!strcmp(argv[2], "auto") ? -1 : atoi(argv[2]));
        SAY("ui: full screen for the report: %s", argv[2]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "color")) { // tests / layout shots: a colour for players without one
        coop_ui::set_test_color((uint32_t)strtoul(argv[2], nullptr, 16));
        SAY("ui: test colour %s", argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "demo")) { // README pictures: documentation address and port
        if (argc > 4)
            coop_ui::set_demo(argv[2], argv[3], argv[4]);
        else
            coop_ui::set_demo("", "", "");
        SAY("ui: demo %s", argc > 4 ? argv[2] : "off");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "widgets")) { // tests: the visible widgets for layout checks
        SAY("%s", coop_ui::widgets_state().c_str());
        return 0;
    }
    if (argc > 1)
        coop_ui::open(atoi(argv[1]) != 0);
    SAY("%s", coop_ui::debug_state().c_str());
    return 0;
}

int c_framefix(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 1)
        g_frame_fix = atoi(argv[1]) != 0;
    SAY("frame order fix %s: calls on a puppet the game no longer lists: anims dropped=%u made=%u, despawns "
        "dropped=%u made=%u; anims for a puppet that is no creature dropped=%u (BUG-0796)\n",
        g_frame_fix ? "on" : "off", g_anims_dropped, g_anims_unlisted, g_despawns_dropped, g_despawns_unlisted,
        g_anims_not_creature);
    return 0;
}

// coop.fix: every fix switch of the mod in one place (fixes.h, BUG-0116)
int c_fix(void*, int argc, const char* const* argv, SkOut* out)
{
    bool ok;
    std::string r = fix::command(std::vector<std::string>(argv + 1, argv + argc), &ok);
    if (ok && argc > 2)
        H->log(M, SK_WARN, "coop.fix %s %s", argv[1], argv[2]);
    for (size_t a = 0, b; a < r.size(); a = b + 1) { // a line per call: out_printf takes 8 KB at most
        b = r.find('\n', a);
        b = b == std::string::npos ? r.size() : b;
        SAY("%s\n", r.substr(a, b - a).c_str());
    }
    return ok ? 0 : 1;
}

void register_commands_core()
{
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.host", "coop.host [port] [name] - host a session", c_host, nullptr, MT);
    H->command_add(M, "coop.join", "coop.join <ip[:port]> [name] - join a host", c_join, nullptr, MT);
    H->command_add(M, "coop.leave", "leave the session", c_leave, nullptr, MT);
    H->command_add(M, "coop.kick", "coop.kick <id | name> - host: remove a player from the session (it is told why; BUG-1108)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", net::kick_command(g_sess, std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.sreport", "coop.sreport [status] | ask | answer yes|no | test none|big|overrun|badname|junk - the session report (F4, BUG-0767): the host asks every guest for its report",
                   sreport::command, nullptr, MT);
    H->command_add(M, "coop.settings", "coop.settings [list] | get <key> | set <key> on|off|default|<n> | force <key> <v> (tests) | stored - the session settings (the host decides; session ones before the start)",
                   settings::command, nullptr, MT);
    H->command_add(M, "coop.lineage",
                   "coop.lineage [share <0-100>] [consent on|off] - old cells as wild species (cell.lineage_cells, BUG-1477): the pool, the offers, the swaps",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", lineage_cells::command(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.progress",
                   "coop.progress [resync] - own progress (all.shared_dna 2, BUG-1469): this game's own DNA / bar / food, every player's bar, the host's records; resync: ask the host for our values again",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "resync"))
                           own_progress::resync();
                       SAY("%s", own_progress::status().c_str());
                       SAY("  dna.cpp: totals_ignored=%u gains_kept=%u deeds_given=%u end_held=%u\n", g_own_totals_ignored,
                           g_own_gains_kept, g_own_deeds_given, own_end_held_count());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.find", "coop.find [colors | color <name|0..7> | project x y z | labels] - F2: the arrows to the partners of the last frame, the player colours, pick a colour",
                   pfind::command, nullptr, MT);
    H->command_add(M, "coop.live", "coop.live - tests (BUG-0426): the object list of this frame (game::listed: walks, hits, destroys seen)",
                   [](void*, int, const char* const*, SkOut* out) {
                       SAY("%s\n", game::live_status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.framefix", "coop.framefix [0|1] - forget a torn-down world's puppets before the frame's messages (tests: 0 = the old order, BUG-0091)",
                   c_framefix, nullptr, MT);
    H->command_add(M, "coop.puppet", "coop.puppet hold on|off | respawn [s] | gamekill | hunger [v] | ownhunger <v> - tests: the remote players' puppets are not moved (a seeded failure for coop_mode.py, BUG-0251); respawn: removed now, shown again next frame or after s seconds (BUG-0453); gamekill: the game destroys the Creature puppets behind the mod's back (BUG-0507)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 2 && !strcmp(argv[1], "speedset")) { // tests (BUG-0788): SetDesiredSpeed on each Creature
                           for (auto& [id, r] : g_remotes) { // puppet now, and +0x1E0 read right after it (same frame)
                               if (!r.puppet || !creature_puppet(r.puppet_adapter) || !game::creature_listed(r.puppet))
                                   continue;
                               uint8_t* sp = (uint8_t*)r.puppet + game::kSpatialOff;
                               float before = *(float*)(sp + 0x1E0);
                               game::set_speed(r.puppet, (float)atof(argv[2]));
                               SAY("puppet %u desired %.2f -> %.2f (standard %.2f)\n", id, before, *(float*)(sp + 0x1E0),
                                   *(float*)(sp + 0x1E4));
                           }
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "speed")) { // tests (BUG-0788): the game's speeds of each Creature puppet
                           for (auto& [id, r] : g_remotes) {
                               if (!r.puppet || !creature_puppet(r.puppet_adapter) || !game::creature_listed(r.puppet))
                                   continue;
                               uint8_t* sp = (uint8_t*)r.puppet + game::kSpatialOff; // cLocomotiveObject
                               net::Vec3 v = game::velocity(r.puppet);
                               SAY("puppet %u desired=%.2f standard=%.2f vel=%.2f want=%.2f err=%.2f kinematic=%d\n", id,
                                   *(float*)(sp + 0x1E0), *(float*)(sp + 0x1E4), drive::speed(v), r.drv.last_want_speed,
                                   r.drv.last_err, (int)r.drv.kinematic);
                           }
                           return 0;
                       }
                       // tests (BUG-1252): ownhunger <v>: the own avatar's food meter (+0xBBC) through the game's setter, as
                       // minutes of its own hunger update leave it (or a meal fills it)
                       if (argc > 2 && !strcmp(argv[1], "ownhunger")) {
                           void* av = game::avatar();
                           if (!av)
                               return 1;
                           float before = game::hunger(av);
                           game::set_hunger(av, (float)atof(argv[2]));
                           SAY("avatar hunger %.1f -> %.1f\n", before, game::hunger(av));
                           return 0;
                       }
                       // tests (BUG-1252): hunger [v]: each Creature puppet's food meter; with v set through the game's setter
                       if (argc > 1 && !strcmp(argv[1], "hunger")) {
                           for (auto& [id, r] : g_remotes) {
                               if (!r.puppet || !creature_puppet(r.puppet_adapter) || !game::creature_listed(r.puppet))
                                   continue;
                               float before = game::hunger(r.puppet);
                               if (argc > 2)
                                   game::set_hunger(r.puppet, (float)atof(argv[2]));
                               SAY("puppet %u hunger %.1f -> %.1f\n", id, before, game::hunger(r.puppet));
                           }
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "hp")) { // tests (BUG-0697): the game's health of each ship puppet
                           for (auto& [id, r] : g_remotes)
                               SAY("puppet %u %s\n", id, modes::space_puppet_hp(r.puppet).c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "respawn")) { // tests (BUG-0453): every partner's puppet
                           int n = 0; // goes now (as on a death or a hide) and is shown again next frame or in <s> s
                           g_puppet_hide_until = argc > 2 ? H->time_seconds() + atof(argv[2]) : 0;
                           for (auto& [id, r] : g_remotes)
                               if (r.puppet) {
                                   despawn(r);
                                   n++;
                               }
                           SAY("%d puppet(s) removed\n", n);
                           return 0;
                       }
                       // tests (BUG-0795): gamekillfx <pool> <hex id> <key> [n]: the game destroys every partner's Creature
                       // puppet (as gamekill) and in the same call that partner's effect message arrives n times
                       if (argc > 4 && !strcmp(argv[1], "gamekillfx")) {
                           int n = argc > 5 ? std::max(1, atoi(argv[5])) : 1, k = 0;
                           for (auto& [id, r] : g_remotes)
                               if (r.puppet && creature_puppet(r.puppet_adapter) && game::creature_listed(r.puppet)) {
                                   net::Vec3 p = game::position(r.puppet);
                                   game::destroy_untracked(r.puppet);
                                   for (int i = 0; i < n; i++)
                                       effects::inject(id, id, 0, (uint8_t)atoi(argv[2]), (uint32_t)strtoul(argv[3], nullptr, 16),
                                                       (uint32_t)strtoul(argv[4], nullptr, 0), p, net::kNoAgl);
                                   SAY("puppet %u (%p) destroyed by the game, %d effect message(s) of it handled\n", id, r.puppet, n);
                                   k++;
                               }
                           SAY("%s", effects::status().c_str());
                           return k ? 0 : 1;
                       }
                       if (argc > 1 && !strcmp(argv[1], "gamekill")) { // tests (BUG-0507): the game destroys
                           int n = 0; // every partner's Creature puppet behind the mod's back (as it did on a
                           for (auto& [id, r] : g_remotes) // player's PC): the mod must notice and show it again
                               if (r.puppet && creature_puppet(r.puppet_adapter)) {
                                   game::destroy_untracked(r.puppet);
                                   n++;
                               }
                           SAY("%d puppet(s) destroyed by the game\n", n);
                           return 0;
                       }
                       if (argc > 1 && (!strcmp(argv[1], "agl") || !strcmp(argv[1], "sink"))) {
                           // tests (BUG-0683): agl: each Creature puppet's height over this game's terrain; sink <depth>
                           // [side]: each is put <side> m sideways and <depth> m under its terrain there (teleport), as
                           // the game's separation of creatures stacked on the nest pressed one in
                           bool sink = !strcmp(argv[1], "sink") && argc > 2;
                           float depth = sink ? (float)atof(argv[2]) : 0, side = sink && argc > 3 ? (float)atof(argv[3]) : 0;
                           for (auto& [id, r] : g_remotes) {
                               if (!r.puppet || !creature_puppet(r.puppet_adapter) || !game::creature_listed(r.puppet))
                                   continue;
                               net::Vec3 q = game::position(r.puppet);
                               float rr = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
                               if (sink && rr > 1.f) {
                                   // a direction across the planet's normal
                                   net::Vec3 n{q.x / rr, q.y / rr, q.z / rr}, ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
                                   net::Vec3 u{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
                                   float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
                                   net::Vec3 w{q.x + side * u.x / ul, q.y + side * u.y / ul, q.z + side * u.z / ul};
                                   float wl = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z), g = game::ground_radius(w);
                                   if (g > 0) {
                                       float k = (g - depth) / wl;
                                       game::teleport(r.puppet, {w.x * k, w.y * k, w.z * k}, game::orientation(r.puppet));
                                   }
                                   q = game::position(r.puppet);
                                   rr = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
                               }
                               float g = rr > 1.f ? game::ground_radius(q) : 0.f;
                               SAY("puppet %u agl=%.2f pos=(%.2f %.2f %.2f) ungrounded=%u\n", id, g > 0 ? rr - g : 0.f, q.x, q.y, q.z,
                                   r.drv.ungrounded);
                           }
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "hold"))
                           g_puppet_hold = !strcmp(argv[2], "on") || !strcmp(argv[2], "1");
                       SAY("puppet hold %s\n", g_puppet_hold ? "on" : "off");
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.stalltest", "coop.stalltest <s> - tests (BUG-0687): the main thread runs a message loop of its own for s seconds (max 60): no frames, the keepalive runs",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       double secs = std::min(argc > 1 ? atof(argv[1]) : 5.0, 60.0), t0 = H->time_seconds();
                       MSG m;
                       while (H->time_seconds() - t0 < secs) {
                           while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                               TranslateMessage(&m);
                               DispatchMessageW(&m);
                           }
                           Sleep(10);
                       }
                       SAY("stalled %.1f s\n", H->time_seconds() - t0);
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME); // (inside a frame the outside-frame tasks, the keepalive too, wait for its end)
    H->command_add(M, "coop.lanfake", "coop.lanfake <n> [seconds] - n made-up games on the network answer (192.0.2.x, tests, BUG-0183)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       g_lan_fake_n = argc > 1 ? std::clamp(atoi(argv[1]), 0, 64) : 0;
                       g_lan_fake_until = net::now_ms() + (uint32_t)(argc > 2 ? atoi(argv[2]) : 60) * 1000u;
                       SAY("fake games %d, listed %zu, kept %zu\n", g_lan_fake_n, g_disc.games(net::now_ms()).size(),
                           g_disc.stored());
                       for (auto& g : g_disc.games(net::now_ms())) // the rows (BUG-0210 test)
                           SAY("  game %s %s id=%08X%s\n", g.info.name.c_str(), g.host.str().c_str(), g.info.host_id,
                               g.info.has_id ? "" : " (none)");
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.slowdns", "coop.slowdns <ms> - every host name lookup waits ms first, like a slow name server (tests, BUG-0204)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       g_slow_dns_ms = argc > 1 ? std::clamp(atoi(argv[1]), 0, 60000) : 0;
                       if (net::g_lookup != slow_lookup) {
                           g_real_lookup = net::g_lookup;
                           net::g_lookup = slow_lookup;
                       }
                       SAY("name lookups wait %d ms\n", g_slow_dns_ms.load());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.settingsin", "coop.settingsin [0|1] - tests: 0 = the host's settings tables are dropped (a host older than 0.7.12, BUG-0762)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1)
                           g_settings_in = atoi(argv[1]) != 0;
                       SAY("settings tables %s\n", g_settings_in ? "taken" : "dropped (tests)");
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.fix", "coop.fix [list] | <name> [on|off] | all on|off - every fix switch (tests: off = the code before the fix)",
                   c_fix, nullptr, MT);
    H->command_add(M, "coop.exittest", "coop.exittest [old] - end the game with ExitProcess(0) without Shutdown (tests, BUG-0107)",
                   c_exittest, nullptr, MT);
    H->command_add(M, "coop.targets",
                   "coop.targets [all|objs] - clickable units on screen: kind id owner x y; all: also every synced "
                   "object: obj key distance x y; objs: also every game object: obj o<noun> distance x y (tests)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", targets(argc < 2 ? 0 : !strcmp(argv[1], "all") ? 1 : !strcmp(argv[1], "objs") ? 2 : 0).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.status", "session, players and puppets", c_status, nullptr, MT);
    // B3 / B6 (BUG-1136, BUG-1137): the message manifest, the family revisions; tests plant a revision and send a crafted
    // message through the session (the direction gate's lab test)
    H->command_add(M, "coop.manifest",
                   "coop.manifest [plant <family> <rev> [minor] | reset | send <type> [hex bytes] | types] - the message "
                   "families of this game and the players (net/messages.def), the host's direction gate; tests: plant a "
                   "family revision, send a crafted message to the host (a guest) or to every guest",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "types")) {
                           for (int t = 0; t < 256; t++) {
                               net::MsgInfo m = net::msg_info((uint8_t)t);
                               if (m.known)
                                   SAY("%3d %-13s dir=%d cls=%d route=%d recv=%d family=%d\n", t, m.name, (int)m.dir,
                                       (int)m.cls, (int)m.route, (int)m.recv, (int)m.family);
                           }
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "reset")) {
                           g_sess.families = net::compat::my_families();
                           SAY("families reset: %s\n", net::compat::families_text(g_sess.families).c_str());
                           return 0;
                       }
                       if (argc > 3 && !strcmp(argv[1], "plant")) { // tests: this game's revision of one family
                           bool hit = false;
                           for (auto& f : g_sess.families)
                               if (!strcmp(argv[2], net::family_info(f.family) ? net::family_info(f.family)->name : "")) {
                                   f.rev = (uint16_t)atoi(argv[3]);
                                   f.minor = argc > 4 ? (uint16_t)atoi(argv[4]) : f.minor;
                                   hit = true;
                               }
                           SAY("%s: %s\n", hit ? "planted" : "no such family", net::compat::families_text(g_sess.families).c_str());
                           return hit ? 0 : 1;
                       }
                       if (argc > 2 && !strcmp(argv[1], "send")) { // tests: a crafted message of any type
                           net::Writer w;
                           for (const char* h = argc > 3 ? argv[3] : ""; h[0] && h[1]; h += 2) {
                               char b[3] = {h[0], h[1], 0};
                               w.u8((uint8_t)strtoul(b, nullptr, 16));
                           }
                           uint8_t type = (uint8_t)strtoul(argv[2], nullptr, 0);
                           if (g_sess.role() == net::Role::None) {
                               SAY("not in a session\n");
                               return 1;
                           }
                           g_sess.broadcast_reliable(type, w);
                           SAY("sent %s (%u), %zu bytes\n", net::msg_name(type), type, w.size());
                           return 0;
                       }
                       SAY("this game: %s\n", net::compat::families_text(g_sess.families).c_str());
                       for (auto& p : g_sess.players())
                           if (p.id != g_sess.my_id())
                               SAY("player %u '%s': %s\n", p.id, p.name.c_str(),
                                   p.version.empty() ? "version unknown" : p.version.c_str());
                       SAY("dir_drops=%u compat_refused=%u dir_gate=%s family_check=%s\n", g_sess.dir_drops(),
                           g_sess.compat_refused(), net::g_fix.dir_gate ? "on" : "off", net::g_fix.family_check ? "on" : "off");
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.effects",
                   "coop.effects [probe <hex key> | map | log on|off | load <n> [period_ms] [hex id] | nopuppet <player> [0|1]] - creature effects "
                   "relayed to the partners' puppets (BUG-0451); probe: the game's own effect pool of a key on the avatar "
                   "and every puppet; load: n effects per period at the local player (the frame-time measurement)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "log")) {
                           SAY("effects log %s\n", effects::set_log(argc > 2 && (!strcmp(argv[2], "on") || !strcmp(argv[2], "1"))) ? "on" : "off");
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "inject")) {
                           // tests: a crafted MSG_EFFECT as if peer `from` sent it (validation)
                           if (argc < 8) {
                               SAY("inject <from> <player> <stop> <pool> <id hex> <key> [x y z [agl]]");
                               return 1;
                           }
                           net::Vec3 p{};
                           if (argc > 10) {
                               p.x = (float)atof(argv[8]);
                               p.y = (float)atof(argv[9]);
                               p.z = (float)atof(argv[10]);
                           }
                           effects::inject((uint8_t)strtoul(argv[2], nullptr, 0), (uint8_t)strtoul(argv[3], nullptr, 0),
                                           (uint8_t)strtoul(argv[4], nullptr, 0), (uint8_t)strtoul(argv[5], nullptr, 0),
                                           (uint32_t)strtoul(argv[6], nullptr, 16), (uint32_t)strtoul(argv[7], nullptr, 0), p,
                                           argc > 11 ? (float)atof(argv[11]) : net::kNoAgl); // BUG-0608: agl
                           SAY("%s", effects::status().c_str());
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "nopuppet")) { // tests (BUG-1517): nopuppet <player> [0|1]
                           effects::test_no_puppet((uint8_t)atoi(argv[2]), argc <= 3 || atoi(argv[3]) != 0);
                           SAY("%s", effects::status().c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "drain")) { // tests (BUG-1252): drain [n] (default 10)
                           effects::drain_tokens(argc > 2 ? atof(argv[2]) : 10.0);
                           SAY("effects: the rate limit is %s tokens in debt\n", argc > 2 ? argv[2] : "10");
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "load")) {
                           uint32_t n = argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 10) : 0;
                           uint32_t ms = argc > 3 ? (uint32_t)strtoul(argv[3], nullptr, 10) : 1000;
                           uint32_t id = argc > 4 ? (uint32_t)strtoul(argv[4], nullptr, 16) : 0;
                           effects::set_load(n, ms, id);
                           SAY("%s", effects::status().c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "watch")) { // tests (BUG-0465): watch <hex id> | watch
                           SAY("%s", effects::watch(argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0).c_str());
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "probe")) {
                           SAY("%s", effects::probe((uint32_t)strtoul(argv[2], nullptr, 16)).c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "recent")) { // tests (BUG-1243)
                           SAY("%s", effects::recent().c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "map")) {
                           SAY("%s", effects::map_dump().c_str());
                           return 0;
                       }
                       SAY("%s", effects::status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.track", "coop.track start|stop|count|dump [from] [n]|lift <m>|novel 0|1 - every frame: the partners' puppets and the own avatar; in Tribe the player tribe's members (tests, BUG-0360, BUG-0365, BUG-0366)",
                   c_track, nullptr, MT);
    H->command_add(M, "coop.log", "coop.log detail [0|1|2] - support logging: 0 = the old lines only, 1 = state trace + events "
                   "(default), 2 = verbose; coop.log trace - write the trace lines now",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc >= 3 && !strcmp(argv[1], "detail"))
                           trace::g_detail = std::max(0, std::min(2, atoi(argv[2])));
                       else if (argc >= 2 && !strcmp(argv[1], "trace"))
                           g_trace_at = g_trace_body_at = g_trace_stage_at = -100; // the next frame writes them
                       SAY("log_detail=%d\n", trace::g_detail);
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.name", "coop.name <nick>", c_name, nullptr, MT);
    H->command_add(M, "coop.say", "coop.say <text> - chat", c_say, nullptr, MT);
    H->command_add(M, "coop.avatar", "print local avatar state (planet stages: ground = terrain radius under it, above = height over it)", c_avatar, nullptr, MT);
    H->command_add(M, "coop.echo", "coop.echo [on|off] [offset] [delay] - loopback puppet test", c_echo, nullptr, MT);
    H->command_add(M, "coop.overlay", "coop.overlay on|off|self", c_overlay, nullptr, MT);
    H->command_add(M, "coop.players", "coop.players [held <id>] - the per-player state modules keep (BUG-0422)", c_players, nullptr, MT);
    H->command_add(M, "coop.guard", "coop.guard [unwind <name> [n] | stick <name>] - the call guards of the modules and what the frame reset cleared (BUG-1047)", c_guard, nullptr, MT);
    H->command_add(M, "coop.testreset", "coop.testreset [fp|plant <n>] - reset every registered session and world state and print the fingerprint of what still holds (D2, the lab's warm reset)", c_testreset, nullptr, MT);
    H->command_add(M, "coop.lifecycle", "coop.lifecycle [held|hooks|world] - the session / world state modules keep, what the last resets left (BUG-1055), the observers and the world identity (BUG-1141)", c_lifecycle, nullptr, MT);
    H->command_add(M, "coop.social", "coop.social [species | log | act <npc id> [slot] | goto <npc id>] - creature social games", c_social, nullptr, MT);
    H->command_add(M, "coop.fields", "coop.fields - replicated state fields of mirrored objects per kind (BUG-0419 G2, fields.h)",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       // tests / RE: `coop.fields scale <hex object> [value]` reads (and sets) an object's scale
                       if (argc > 2 && !strcmp(argv[1], "scale")) {
                           void* o = (void*)(uintptr_t)strtoul(argv[2], nullptr, 16);
                           if (!o || IsBadReadPtr(o, 0x40) || !game::object_alive(o)) {
                               SAY("no object %s\n", argv[2]);
                               return 0;
                           }
                           if (argc > 3)
                               game::set_object_scale(o, (float)atof(argv[3]));
                           SAY("scale %p %.3f\n", o, game::object_scale(o));
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "list")) {
                           std::string l = fields::list(argv[2]);
                           for (size_t at = 0; at < l.size(); at += 6000) // out_printf keeps 8 KB per call
                               SAY("%s", l.substr(at, 6000).c_str());
                           return 0;
                       }
                       SAY("%s", fields::status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.npc", "coop.npc [on|off|list|herds|hit <id> <dmg>|gone <id> [secs]|hosthit <id> <dmg> [push]|recruit <id>|spawn <hex>|cinedefer on|off] - host-authoritative NPC sync", c_npc, nullptr, MT);
    H->command_add(M, "coop.res", "coop.res [[joinsim] add <food|money> <n> | fix reset|floor [0|1]] - shared resources", c_res, nullptr, MT);
    H->command_add(M, "coop.ping", "coop.ping - show everybody the place under your mouse / where you are", c_ping,
                   nullptr, MT);
    H->command_add(M, "coop.share", "coop.share [player id|due] - (re)send the host world (save) to guests (due: tests, as after an evolution)", c_share,
                   nullptr, MT);
    H->command_add(M, "coop.perf", "coop.perf readable [extra <n>] - tests (BUG-0791): readable() calls per frame since the last ask, their cost (us per call); extra: n more VirtualQuery per call",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       static LONG s_calls;
                       static uint64_t s_frame;
                       static double s_t;
                       if (argc > 3 && !strcmp(argv[2], "extra"))
                           skg::readable_extra = atoi(argv[3]);
                       LONG c = skg::readable_calls;
                       uint64_t f = H->frame_index();
                       double t = H->time_seconds();
                       double frames = (double)(f - s_frame), secs = t - s_t;
                       // the cost of one call on a heap block and a stack address (the main thread's own pointers)
                       static int s_heap[16];
                       LARGE_INTEGER q0, q1, qf;
                       QueryPerformanceFrequency(&qf);
                       QueryPerformanceCounter(&q0);
                       int ok = 0;
                       for (int i = 0; i < 2000; i++)
                           ok += skg::readable(&s_heap[i & 15], 4);
                       QueryPerformanceCounter(&q1);
                       double us = (double)(q1.QuadPart - q0.QuadPart) * 1e6 / (double)qf.QuadPart / 2000.0;
                       SAY("readable calls=%ld since=%ld frames=%.0f secs=%.1f per_frame=%.1f fps=%.1f us_per_call=%.3f extra=%d ok=%d\n",
                           (long)c, (long)(c - s_calls), frames, secs, frames > 0 ? (c - s_calls) / frames : 0.0,
                           secs > 0 ? frames / secs : 0.0, us, skg::readable_extra, ok);
                       s_calls = skg::readable_calls; // (the 2000 calls of the benchmark not counted for the next ask)
                       s_frame = f;
                       s_t = t;
                       return 0;
                   },
                   nullptr, MT);
    auto adv_cmd = [](void*, int argc, const char* const* argv, SkOut* out) {
        std::vector<std::string> a;
        for (int i = 1; i < argc; i++)
            a.emplace_back(argv[i]);
        std::string s = adventure::command(a);
        SAY("%s", s.c_str());
        return s == "no adventure" ? 1 : 0;
    };
    const char* adv_help =
        "coop.adventure [status|goals|actors|bodies|act <n>|place <hex>|credit <slot>|state <5|6>|hit <id> <dmg>|kill <id>|spawn_extra] - co-op Galactic Adventures status and test controls (BUG-0845, BUG-1474, BUG-0793)";
    H->command_add(M, "coop.adventure", adv_help, adv_cmd, nullptr, MT);
    H->command_add(M, "coop.adv", adv_help, adv_cmd, nullptr, MT);
    H->command_add(M, "coop.awaytest", "coop.awaytest 1|0 - tests (BUG-1084, a guest): the host counts as in an editor (away after 4 s) until 0",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1)
                           g_away_test = atoi(argv[1]) != 0;
                       SAY("awaytest=%d host_away=%d", (int)g_away_test, (int)g_host_away);
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.leaving", "coop.leaving - tests (BUG-0264): the avatar while the game leaves a stage for the galaxy menu",
                   [](void*, int, const char* const*, SkOut* out) {
                       const auto& l = game::leave_stats();
                       SAY("leaving=%d windows=%u frames=%u raw_gone=%u used_gone=%u refused=%u bad_vtable=%u stage_gone=%u",
                           (int)game::leaving_stage(), l.windows, l.frames, l.raw_gone, l.used_gone, l.refused, l.bad_vtable,
                           l.stage_gone);
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.partnerpass", "coop.partnerpass - tests (BUG-0913): the partners' creatures we walk and click through",
                   [](void*, int, const char* const*, SkOut* out) {
                       int n = 0;
                       for (void* p : g_pass_puppets)
                           n += p != nullptr;
                       SAY("fix=%d on=%d puppets=%d pairs_skipped=%u own_pairs_seen=%u hover_skipped=%u ff=%d "
                           "phantom_pairs=%u/%u/%u social_hover=%d social_hover_skipped=%u",
                           (int)g_partner_pass_fix, (int)g_pass_on, n, g_pass_pairs, g_pass_seen_own, g_pass_hover_skipped,
                           (int)settings::on(settings::kFriendlyFire), g_pass_phantom_pairs, g_pass_phantom_own,
                           g_pass_phantom_partner, (int)g_social_hover_on, g_social_hover_skipped); // BUG-1250
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.cellgone", "coop.cellgone - tests (BUG-0271): cell reads refused once the game freed the cell pool",
                   [](void*, int, const char* const*, SkOut* out) {
                       const auto& c = modes::cell_gone_stats();
                       SAY("fix=1 checks=%u outside=%u pool_gone=%u refused=%u windows=%u frames_gone=%u local_refused=%u "
                           "puppet_refused=%u",
                           c.checks, c.outside, c.pool_gone, c.refused, c.windows, c.frames_gone,
                           g_local_refused, g_puppet_refused);
                       return 0;
                   },
                   nullptr, SK_CMD_ANY_THREAD);
}

void register_commands_editor()
{
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.hoststate", "coop.hoststate mode <hex> <seconds> - tests: our player states report this mode",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 3 && !strcmp(argv[1], "mode")) {
                           g_state_mode_override = (uint32_t)strtoul(argv[2], nullptr, 16);
                           g_state_mode_until = H->time_seconds() + atof(argv[3]);
                       }
                       SAY("state mode override %08X for %.1f s\n", g_state_mode_override,
                           std::max(0.0, g_state_mode_until - H->time_seconds()));
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.editor", "coop.editor - tests (BUG-0368): the open editor's request and the parts its palette shows locked",
                   [](void*, int, const char* const*, SkOut* out) {
                       // the editor game mode: the mode manager's list (+0x14..+0x18, entries of 0x18 bytes
                       // {mode, id, name}) holds it under id 0xDBDBA1 (0x57F3E0); its request at +0x1CC
                       // (OnEnter reads +0x94 there and gives it to the palette)
                       void* mm = skg::cdecl_call<void*>(H->sym("App::IGameModeManager::Get"));
                       void* ed = nullptr;
                       if (mm && skg::readable((char*)mm + 0x14, 8)) {
                           auto* b = skg::field<uint32_t*>(mm, 0x14);
                           auto* e = skg::field<uint32_t*>(mm, 0x18);
                           for (uint32_t* it = b; it && it < e && e - b < 6 * 64 && skg::readable(it, 8); it += 6)
                               if (it[1] == 0xDBDBA1u)
                                   ed = (void*)(uintptr_t)it[0];
                       }
                       void* req = ed && skg::readable((char*)ed + 0x1CC, 4) ? skg::field<void*>(ed, 0x1CC) : nullptr;
                       if (!req || !skg::readable(req, 0x9C)) {
                           SAY("no editor request (mode %p)\n", ed);
                           return 1;
                       }
                       void* items = skg::field<void*>(req, 0x94);
                       void* mine = collected_parts();
                       // the palette's rule (0x5C6820): available = no items object, or the item unlocked
                       // (IsUnlocked); counted over the unlockable items of the collected parts (parts::census:
                       // the map at +0x1490 and, BUG-0441, the item states)
                       int unlockable = 0, locked = 0, avail = 0;
                       uint32_t avail_hash = 0;
                       if (items && parts::census(avail, avail_hash, unlockable, nullptr, nullptr, items))
                           locked = unlockable - avail;
                       else if (!items && mine && parts::census(avail, avail_hash, unlockable, nullptr, nullptr, mine)) {
                           avail = unlockable; // no items: every part offered
                           avail_hash = 0;
                       }
                       SAY("editor %08X key %08X request %p items %p collected %p unlockable %d locked %d (own editor items set %u) "
                           "available=%d:%08X\n",
                           skg::field<uint32_t>(req, 0xC), skg::field<uint32_t>(req, 0x10), req, items, mine, unlockable,
                           locked, g_own_editor_items_set, avail, avail_hash);
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.parts", "coop.parts [unlock <n>|find <level>|keys|states|cell [collect <i>]] - the collected creature parts as the trace's parts= field counts them; "
                   "unlock <n>: unlock n locked ones in this game only (tests: a player's unlock the other games miss); "
                   "find <level>: the game's own 'a part was found' (a skeleton's reward, BUG-0401); keys: the unlocked unlockable keys; states: every item state (BUG-0441)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "cell")) { // BUG-0467: cell [collect <i>]
                           std::vector<std::string> a;
                           for (int i = 2; i < argc; i++)
                               a.push_back(argv[i]);
                           SAY("%s", parts::cell_debug(a).c_str());
                           return 0;
                       }
                       std::vector<std::pair<uint32_t, uint32_t>> locked;
                       int n = 0, all = 0, done = 0;
                       uint32_t h = 0;
                       if (!parts_census(n, h, all, &locked)) {
                           SAY("no collected parts (no cPlayer)\n");
                           return 1;
                       }
                       if (argc > 1 && (!strcmp(argv[1], "keys") || !strcmp(argv[1], "states"))) {
                           SAY("%s", parts::dump(argv[1]).c_str());
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "unlock")) {
                           // cCollectableItems::UnlockPart(key, cost 0), as when the player finds the part
                           uintptr_t unlock = H->sym("Simulator::cCollectableItems::UnlockPart");
                           void* items = collected_parts();
                           for (int i = 0; unlock && items && i < atoi(argv[2]) && i < (int)locked.size(); i++)
                               done += skg::thiscall<bool>(unlock, items, locked[i].first, locked[i].second, 0) ? 1 : 0;
                           parts_census(n, h, all);
                       } else if (argc > 2 && !strcmp(argv[1], "find")) {
                           int before = n, after = parts::find_part(atoi(argv[2]));
                           done = after >= 0 ? after - before : 0;
                           parts_census(n, h, all);
                       }
                       SAY("parts=%d:%08X unlockable=%d unlocked_now=%d\n", n, h, all, done);
                       SAY("sync: %s\n", parts::status().c_str());
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME); // find shows the game's goal card
    H->command_add(M, "coop.editreq", "coop.editreq diff - tests (BUG-0720): the creature stage's own evolution request (its handler run once, nothing submitted) against the ones the mod builds",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       void* av = game::avatar();
                       uintptr_t flag = H->sym("SporeCoop::CreatureEvolutionPending_flag");
                       if (argc > 2 && !strcmp(argv[1], "keep")) { // exploration: which own-editor bytes give its cross
                           g_own_keep_ctor = (unsigned)strtoul(argv[2], nullptr, 0);
                           SAY("own editor keeps the constructor's bytes: mask %u\n", g_own_keep_ctor);
                           return 0;
                       }
                       if (argc < 2 || strcmp(argv[1], "diff") || game::mode_id() != modes::kCreature || !av || !g_creature_game ||
                           !o_CreatureGameMsg || !flag) {
                           SAY("not possible here (creature=%d creature_game=%p)\n", (int)(game::mode_id() == modes::kCreature), g_creature_game);
                           return 1;
                       }
                       // the game's builder: message 0x4471C60 with +0xDA set (the avatar met its mate), Submit captured
                       uint8_t& da = skg::field<uint8_t>(g_creature_game, 0xDA);
                       uint8_t da0 = da, f0 = *(uint8_t*)flag;
                       da = 1;
                       g_editreq_game = {};
                       g_editreq_capture = true;
                       o_CreatureGameMsg(g_creature_game, 0x4471C60u, nullptr);
                       g_editreq_capture = false;
                       da = da0;
                       *(uint8_t*)flag = f0; // its "evolution pending" byte back (nothing was submitted)
                       if (!g_editreq_game.have) {
                           SAY("the game built no request\n");
                           return 1;
                       }
                       // fields that differ by nature: vtables +0/+4, refcount +8, the strings' pointers +0x40..+0x5C,
                       // the vector's pointers and allocator +0x70..+0x83 (its contents are compared), and the bytes
                       // the constructor never writes (padding after its byte fields: +0x21..+0x23, +0x3E/+0x3F, +0x67;
                       // the allocator's leftovers, seen different in the lab 20261108-035619)
                       auto skip = [](int o) {
                           return o < 0xC || (o >= 0x21 && o <= 0x23) || o == 0x3E || o == 0x3F || (o >= 0x40 && o < 0x60) || o == 0x67 ||
                                  (o >= 0x70 && o < 0x84);
                       };
                       for (EditKind kind : {EditKind::Own, EditKind::Evolve}) {
                           void* req = build_editor_request(kind, 0, game::species_key(av));
                           ReqImage ours = req_image(req);
                           if (req) // the caller's reference: freed
                               skg::thiscall<void>(skg::field<uintptr_t>(skg::field<void*>(req, 0), 8), req);
                           std::string diff, allowed;
                           char b[64];
                           for (int o = 0; o < 0x9C && ours.have; o++) {
                               if (skip(o) || ours.bytes[o] == g_editreq_game.bytes[o])
                                   continue;
                               int field = o & ~3;
                               // the own editor has no evolution tag (BUG-0466) and keeps the constructor's +0x39 / +0x3B / +0x3C
                               // (its red cross and save dialog, BUG-0756)
                               bool allow = kind == EditKind::Own && (field == 0x90 || o == 0x39 || o == 0x3B || o == 0x3C);
                               snprintf(b, sizeof b, " +0x%02X(%02X/%02X)", o, ours.bytes[o], g_editreq_game.bytes[o]);
                               (allow ? allowed : diff) += b;
                           }
                           if (ours.vec70 != g_editreq_game.vec70) {
                               snprintf(b, sizeof b, " vec70(%u/%u)", (unsigned)ours.vec70.size(), (unsigned)g_editreq_game.vec70.size());
                               diff += b;
                           }
                           SAY("editreq %s: diff=%s allowed=%s (vec70 %u, items %08X/%08X)\n", kind == EditKind::Own ? "own" : "evolve",
                               diff.empty() ? " none" : diff.c_str(), allowed.empty() ? " none" : allowed.c_str(),
                               (unsigned)g_editreq_game.vec70.size(), *(uint32_t*)(ours.bytes + 0x94), *(uint32_t*)(g_editreq_game.bytes + 0x94));
                       }
                       SAY("editreq fix=%d; the game's: post 0x5D02A72 and byte 0x1686AF0 (allowed: the evolution flow's, not the own editor's)\n",
                           (int)g_editor_request_fix);
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    // BUG-1436 (tests): the game's Cell -> Creature switch as the game calls it (History's check, the end sequence's
    // event 0x1F: through our hook h_CellToCreature); `quit`: the mod's quit to the galaxy menu (as for a received
    // world), and the switch right after the game handled the quit message (h_SimMsg), while the stage is left
    H->command_add(M, "coop.cellswitch", "coop.cellswitch [quit] - tests (BUG-1436): the game's Cell -> Creature switch through the hook; quit: "
                   "the mod's quit to the galaxy menu first, the switch right after the game handled it",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (game::mode_id() != modes::kCell) {
                           SAY("not in the Cell stage (dropped so far %u)", g_quit_switch_dropped);
                           return 1;
                       }
                       if (argc > 1 && !strcmp(argv[1], "quit")) {
                           std::string err;
                           if (!world::post_quit_to_menu(err)) {
                               SAY("quit refused: %s", err.c_str());
                               return 1;
                           }
                           g_cellswitch_after_quit = true;
                           SAY("quit posted: the switch follows when the game handles it (dropped so far %u)", g_quit_switch_dropped);
                           return 0;
                       }
                       unsigned before = g_quit_switch_dropped;
                       h_CellToCreature();
                       SAY("switch called (leaving the stage %d): %s (dropped %u)", (int)game::leaving_stage(),
                           g_quit_switch_dropped != before ? "not taken" : "passed to the game", g_quit_switch_dropped);
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    H->command_add(M, "coop.vote", "coop.vote [test|go|ship|tribeend [<archetype hex>]|noavatar <s>] - evolution vote (test: a vote without an editor; ship: Civ -> Space spaceship editor request; tribeend: the end-of-Tribe editor guard; noavatar: a vote end in <s> sees no avatar, BUG-1421; tests)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "test") && g_sess.role() == net::Role::Host && !g_vote.active) {
                           start_vote(nullptr, g_name + " wants to evolve (test)");
                       } else if (argc > 1 && !strcmp(argv[1], "ship") && game::mode_id() == modes::kCiv) {
                           // tests (BUG-1531): native Civ -> Space spaceship EditorRequest (0xCF5A44..0xCF5ACB) through the vote hook
                           uintptr_t ctor = H->sym("EditorRequest_ctor"), alloc = H->sym("GeneralAllocator::new_"),
                                     submit = H->sym("Editors::EditorRequest::Submit");
                           if (!ctor || !alloc || !submit) {
                               SAY("not possible here");
                               return 1;
                           }
                           void* req = skg::cdecl_call<void*>(alloc, (size_t)0x9C, "EditorRequest", 0, 0, 0, 0);
                           if (!req) {
                               SAY("not possible here");
                               return 1;
                           }
                           skg::thiscall<void*>(ctor, req);
                           skg::vcall<int>(req, 1); // AddRef
                           skg::field<uint32_t>(req, 0xC) = designs::kShipEditor;
                           skg::field<uint32_t>(req, 0x90) = 0x0116D51Du;
                           skg::field<uint8_t>(req, 0x34) = 0;
                           skg::field<uint8_t>(req, 0x36) = 1;
                           skg::field<uint8_t>(req, 0x37) = 1;
                           skg::field<uint8_t>(req, 0x38) = 1;
                           skg::field<uint8_t>(req, 0x39) = 0;
                           skg::field<uint8_t>(req, 0x3B) = 0;
                           skg::field<uint8_t>(req, 0x3C) = 1;
                           skg::field<uint8_t>(req, 0x3D) = 1;
                           skg::field<uint8_t>(req, 0x64) = 1;
                           bool r = skg::cdecl_call<bool>(submit, req);
                           skg::vcall<int>(req, 2); // Release
                           SAY("ship editor request submitted -> %d\n", (int)r);
                       } else if (argc > 1 && !strcmp(argv[1], "editor")) {
                           // a request through the vote (the hook), like the game's own editor entry
                           uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit");
                           void* av = game::avatar();
                           if (!av && game::mode_id() == modes::kTribe) // tests: a member's species (BUG-0217)
                               tribe::for_each_owned([](void* c, uint8_t, void* u) { if (!*(void**)u) *(void**)u = c; }, &av);
                           if (!ctor || !submit || !av) {
                               SAY("not possible here");
                               return 1;
                           }
                           void* req = build_editor_request(EditKind::Edit, argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0,
                                                            game::species_key(av)); // BUG-0720
                           bool r = req && skg::cdecl_call<bool>(submit, req);
                           SAY("editor request submitted -> %d\n", (int)r);
                       } else if (argc > 1 && (!strcmp(argv[1], "evolve") || !strcmp(argv[1], "stageend")) &&
                                  game::mode_id() == modes::kCreature) {
                           // tests (BUG-0457): the creature stage's own evolution request, as its message handler
                           // 0xD42E50 builds it when the avatar meets its mate (docs/re/creations.md#creations-evolution-request):
                           // +0x90 the evolution tag (the editor's result is applied to the species), +0x94 the
                           // collected parts, the flags, +0x70 cPlayer +0x10FC..+0x1108; through the vote (the hook)
                           uintptr_t ctor = H->sym("EditorRequest_ctor"), submit = H->sym("Editors::EditorRequest::Submit"),
                                     assign = H->sym("SporeCoop::IntrusivePtr_Assign"), push = H->sym("SporeCoop::VectorU32_PushBack"),
                                     nouns = H->sym("Simulator::cGameNounManager::Get"), getp = H->sym("SporeCoop::cGameNounManager_GetPlayer");
                           void* av = game::avatar();
                           void* nm = nouns ? skg::cdecl_call<void*>(nouns) : nullptr;
                           void* pl = nm && getp ? skg::thiscall<void*>(getp, nm) : nullptr;
                           if (!ctor || !submit || !assign || !push || !av || !pl || !skg::readable((char*)pl + 0x10E8, 0x24)) {
                               SAY("not possible here");
                               return 1;
                           }
                           void* req = build_editor_request(EditKind::Evolve, 0, game::species_key(av)); // BUG-0720: the shared builder
                           if (!req) {
                               SAY("not possible here");
                               return 1;
                           }
                           // BUG-1085: `evolve` carries the mating editor's tag (an evolution inside the stage, as 0xD3C650
                           // builds it), `stageend` the stage-end editor's (0xD42E50 on 0x4471C60, Creature -> Tribe)
                           bool end = !strcmp(argv[1], "stageend");
                           skg::field<uint32_t>(req, 0x90) = end ? kTagStageEnd : kTagMating;
                           bool r = skg::cdecl_call<bool>(submit, req);
                           if (uintptr_t f = H->sym("SporeCoop::CreatureEvolutionPending_flag")) // as 0xD43069
                               *(uint8_t*)f = 1;
                           SAY("%s request submitted -> %d\n", end ? "stage-end" : "evolution", (int)r);
                       } else if (argc > 1 && !strcmp(argv[1], "mate") && game::mode_id() == modes::kCreature) {
                           // tests (BUG-0920): the game's own evolution request, not one built by the mod: its message
                           // handler 0xD42E50 run with message 0x4471C60 and +0xDA set (the avatar met its mate, as
                           // coop.editreq diff does), the request goes through the Submit hook like a player's mating
                           // call; the game then posts 0x5D02A72 and sets its editor-pending flag itself. BUG-1085: this
                           // builder is the stage-end editor's (tag 0x64AA168, Creature -> Tribe), not the mating call's
                           if (!game::avatar() || !g_creature_game || !o_CreatureGameMsg) {
                               SAY("not possible here");
                               return 1;
                           }
                           uint8_t& da = skg::field<uint8_t>(g_creature_game, 0xDA);
                           uint8_t da0 = da;
                           da = 1;
                           o_CreatureGameMsg(g_creature_game, 0x4471C60u, nullptr);
                           da = da0;
                           SAY("the game's own evolution request ran (editor-pending flag %d)\n", evo_flag_now());
                       } else if (argc > 1 && !strcmp(argv[1], "action") && g_sess.role() == net::Role::Host) {
                           // an action vote like the superweapon's (tests, BUG-0043)
                           start_action_vote(g_name + " wants to test an action", "the test action runs",
                                             [] { LOG("the test vote action ran"); });
                       } else if (argc > 1 && !strcmp(argv[1], "tribeend")) { // tests (BUG-1280)
                           SAY("%s", tribe_end_line(argc > 2, argc > 2 ? (uint32_t)strtoul(argv[2], nullptr, 16) : 0).c_str());
                           return 0;
                       } else if (argc > 1 && !strcmp(argv[1], "go"))
                           finish_vote("the host started it");
                       else if (argc > 2 && !strcmp(argv[1], "hold")) // tests (BUG-0234): a due own editor waits
                           g_own_editor_hold_until = H->time_seconds() + atof(argv[2]);
                       else if (argc > 2 && !strcmp(argv[1], "noavatar")) // tests (BUG-1421): a vote's end sees no avatar
                           g_vote_noavatar_until = H->time_seconds() + atof(argv[2]);
                       SAY("vote %s: %s (%d s)\nown editor %d, part list waived %u (own outfitters %u, BUG-1308), editor held for a save %u, "
                           "quit held for a save %u, repeated requests dropped %u, own editor due %d (waited %u frames)\n"
                           "actions run %u, votes dropped %u\neditor held for a cinematic %u, cinematics ended %u, "
                           "editor held now %d, actions waited %u, action due %d\n"
                           "editor under the game's own pause %u (own busy %s, editor pause count %d)\n"
                           "votes of the host not taken elsewhere %u\n"
                           "switch votes %u (host clicks held %u, guest clicks held %u, arrivals ready %u)\n"
                           "evolve offers sent %u got %u taken %u expired %u, offer now %d\n"
                           "editor-pending flag %d, cleared %u (BUG-0920)\n"
                           "stage-end votes %u, guest stage-end requests refused %u (BUG-1085)\n"
                           "own editors kept for an arrival %u, arriving now %d (BUG-1240)\n"
                           "votes that stopped waiting for a guest in its editor %u (BUG-1239)\n"
                           "mates taken out while the vote holds the Cell editor %u (BUG-1245)\n"
                           "arrival votes that did not wait for a guest on its way %u (BUG-1245)\n"
                           "own editors from the world's species without the replace dialog %u (BUG-1255)\n"
                           "host Cell editors opened without a vote %u (BUG-1263)\n"
                           "guest mates forgotten in creature.mode host (Cell) %u (BUG-1265)\n"
                           "host History checks held before the end sequence %u (BUG-1269)\n"
                           "arrival votes that waited for a guest arriving quickly %u (BUG-1336)\n"
                           "guests removed for a game that stood during a vote %u (BUG-1420)",
                           g_vote.active ? "active" : "none", g_vote.text.c_str(), g_vote.left, (int)g_own_editor,
                           g_part_list_waivers, g_outfit_part_waivers, g_submits_held, g_quits_delayed, g_submits_repeated,
                           g_own_editor_due ? 1 : 0, g_own_editor_waits, g_vote_actions_run, g_votes_dropped,
                           g_submits_after_cine, g_submit_cines_ended, g_submit_later ? 1 : 0, g_vote_actions_waited,
                           g_vote_action_due ? 1 : 0, g_submits_own_pause, busy::names(g_req_own_busy).c_str(),
                           editor_pause_count(), g_vote_elsewhere, g_switch_votes, g_switch_held, g_switch_guest_held,
                           g_switch_arrivals, g_offers_sent, g_offers_got, g_offers_taken, g_offers_expired,
                           g_offer_editor ? 1 : 0, evo_flag_now(), g_evo_flags_cleared, g_stage_end_votes,
                           g_stage_end_guest_refused, g_own_editor_arrivals, g_own_editor_arriving ? 1 : 0,
                           vote_in_editor_count(), vote_mates_held(), arrival_not_waited(), own_no_replace_count(),
                           cell_editor_now_count(), refused_mates_count(), switch_end_held_count(), arrival_waited(),
                           vote_stuck_kicks());
                       return 0;
                   },
                   // outside the frame: `editor` submits an editor request, which the game does
                   // from its update, never inside Present (BUG-0096)
                   nullptr, SK_CMD_OUTSIDE_FRAME);
}

void register_commands_civ()
{
    H->command_add(M, "coop.wallet", "coop.wallet [income <n>] - Civ own money (civ.own_money, BUG-1468): the wallets; income <n>: tests, the host's game earns a city income of n now",
                   civ_wallet::command, nullptr, SK_CMD_MAIN_THREAD);
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.role", "coop.role [0-3 | fix 0|1] - civilization role: 0 none, 1 military, 2 economic, 3 religious",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 2 && !strcmp(argv[1], "fix")) { // tests: 0 = before BUG-0064 / BUG-0079
                           g_role_fix = atoi(argv[2]) != 0;
                           SAY("role fix %s\n", g_role_fix ? "on" : "off");
                           return 0;
                       }
                       if (argc > 1)
                           set_role((uint8_t)(atoi(argv[1]) & 3));
                       for (auto& p : g_sess.players())
                           SAY("role %u %s %s\n", p.id, p.name.c_str(), role_name(g_roles[p.id]));
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.give", "coop.give [player id | next | target | fix 0|1] - give the selected own tribe members / vehicles (or the opened city); next: the next recipient",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "next")) { // the co-op window's ">" (BUG-0075)
                           give_target_cycle();
                           SAY("give target: %u %s", give_target(), g_sess.name_of(give_target()).c_str());
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "fix")) {
                           g_give_pick_fix = atoi(argv[2]) != 0;
                           SAY("give pick fix %d", (int)g_give_pick_fix);
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "target")) {
                           SAY("give target: %u %s", give_target(), g_sess.name_of(give_target()).c_str());
                           return 0;
                       }
                       uint8_t to = argc > 1 ? (uint8_t)atoi(argv[1]) : give_target();
                       SAY("given: %d", give_selected_to(to));
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.creations", "coop.creations [list | next|prev <creature|cell|ufo> | choose <kind> <hex>]",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", creations::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.depth", "coop.depth [status | list | select <idx> | nudge <delta>] - Creature editor depth handle (BUG-0871)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", editor_depth::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.library", "coop.library [status | list | foreign on|off | scan | retry | feed <lib|my> | ingest <path>] - shared creations library (BUG-0873)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", creations::library_debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.bodyrec", "coop.bodyrec start|stop|count|dump [from] [n] - every frame: every synchronized body as drawn, absolute time (tests, BUG-0425)",
                   c_bodyrec, nullptr, MT);
    H->command_add(M, "coop.sync", "coop.sync - pose of every synchronized object (key x y z qx qy qz qw), tests",
                   [](void*, int, const char* const*, SkOut* out) {
                       std::vector<syncdbg::Entry> v = sync_all();
                       for (auto& e : v) // one line per call: the console buffer of a call is 8 KB
                           SAY("%s", syncdbg::format({e}).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.notify", "coop.notify <text> - show a co-op notification (game event log)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       std::string t;
                       for (int i = 1; i < argc; i++)
                           t += (i > 1 ? " " : "") + std::string(argv[i]);
                       feed(t);
                       SAY("%s", notify::status().c_str());
                       return 0;
                   },
                   nullptr, MT);
}

void register_commands_cell()
{
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.pool", "coop.pool [bite | food <n> | hp <n> | hurt <player> <dmg>] - cell co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", cellsync::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
}

void register_commands_stages()
{
    const int MT = SK_CMD_MAIN_THREAD;
    H->command_add(M, "coop.update", "coop.update [check | apply | url <u> | testdelay <ms> | applydelay <ms>] - SporeCoop updates from GitHub releases",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", autoupdate::command(argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "").c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.space",
                   "coop.space [hit <dmg> | hp | jump [id] | give <n> | spice [add <n>] | terra <a> <w> <t> | planet | empire [colony <PlanetID> [spice] | contact <StarID>]]"
                   " - space co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", spacecoop::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    H->command_add(M, "coop.fleet", "coop.fleet [list | shoot <id> <dmg> | hurt <ship id> <player> [dmg] | spawn <type>] - space co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", squadron::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.civ",
                   "coop.civ [select <id> <0|1> | move <id> <x> <y> <z> | buy <city> <loco> | build <city> <kind>]",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1) {
                           SAY("%s", civ::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                           return 0;
                       }
                       SAY("%s%s", civ::status().c_str(), civ::list().c_str());
                       return 0;
                   },
                   nullptr, MT);
    // WP-K (BUG-1303): every partner's cocoon as this game shows it; `matexy`: the mate the game picked, on the screen
    H->command_add(M, "coop.cocoon",
                   "coop.cocoon [<player id> | matexy] - a partner in its own editor in the game's cocoon (BUG-1303): its "
                   "puppet's animation, the glow, the counters; matexy: the drama manager's mate where the screen draws it (tests)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1 && !strcmp(argv[1], "matexy")) {
                           void* mate = game::mode_id() == modes::kCreature ? drama::mate() : nullptr;
                           if (!mate) {
                               SAY("mate none\n");
                               return 1;
                           }
                           net::Vec3 p = game::position(mate);
                           float w[3] = {p.x, p.y, p.z}, sc[2] = {0, 0};
                           bool vis = H->world_to_screen(w, sc) != 0;
                           SAY("mate %p at %.2f %.2f %.2f screen %s%.0f %.0f dist %.1f\n", mate, p.x, p.y, p.z, vis ? "" : "off ",
                               sc[0], sc[1], game::avatar() ? net::dist(p, game::position(game::avatar())) : -1.f);
                           return 0;
                       }
                       if (argc > 2 && !strcmp(argv[1], "play")) { // play <hex> [<how>] [<player id>]
                           int how = argc > 3 ? atoi(argv[3]) : 0;
                           int pid = argc > 4 ? atoi(argv[4]) : -1;
                           SAY("%s", cocoon::probe_play((uint32_t)strtoul(argv[2], nullptr, 16), how, pid).c_str());
                           return 0;
                       }
                       if (argc > 1 && !strcmp(argv[1], "anim")) {
                           SAY("%s", cocoon::anim_of(argc > 2 ? atoi(argv[2]) : -1).c_str());
                           return 0;
                       }
                       SAY("%s", cocoon::status(argc > 1 ? (uint8_t)atoi(argv[1]) : 0xFF).c_str());
                       SAY("%s", guest_mating_status().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.drama", "coop.drama [start <kind> | mask <hex> | mate | target [<tribe obj hex>]] - the game's world events (BUG-0486, BUG-1078)",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       SAY("%s", drama::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(M, "coop.tribe",
                   "coop.tribe [select <id> <0|1> | walk <x> <y> <z> | act <action> <target id>] - tribe co-op",
                   [](void*, int argc, const char* const* argv, SkOut* out) {
                       if (argc > 1) {
                           SAY("%s", tribe::debug(std::vector<std::string>(argv + 1, argv + argc)).c_str());
                           return 0;
                       }
                       SAY("%s%s", tribe::status().c_str(), tribe::list().c_str());
                       return 0;
                   },
                   nullptr, MT);
    H->command_add(
        M, "coop.evokey", "coop.evokey [nosave|save] - BUG-0517 tests: keys set after an editor; the host's in-place save off/on",
        [](void*, int argc, const char* const* argv, SkOut* out) -> int {
            if (argc > 1)
                g_evo_save_test_off = !strcmp(argv[1], "nosave");
            SAY("evokey: keys_set=%u done=%d watching=%d save_off=%d", g_evo_keys_set, (int)g_evo_key_done,
                (int)(g_evo_key_until > 0), (int)g_evo_save_test_off);
            return 0;
        },
        nullptr, SK_CMD_MAIN_THREAD);
    H->command_add(M, "coop.team", "coop.team [on|off] - co-op mechanics (joint hunt, rescue, pack regen)", c_team,
                   nullptr, MT);
    H->command_add(M, "coop.revive",
                   "coop.revive [down | giveup] - the Creature stage's downed creatures (BUG-1472); down: a lethal hit on the own "
                   "creature through the game's damage path (tests); giveup: the own downed creature gives up (as holding G)",
                   [](void*, int argc, const char* const* argv, SkOut* out) -> int {
                       SAY("%s", revive::command(std::vector<std::string>(argv, argv + argc)).c_str());
                       return 0;
                   },
                   nullptr, MT);
}

void register_commands_ui()
{
    H->command_add(M, "coop.adapters", "coop.adapters [stress N|fault] - local address lookup (BUG-0286)",
                   [](void*, int argc, const char* const* argv, SkOut* out) { return adapters::command(argc, argv, out); },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    H->command_add(M, "coop.uistate", "coop.uistate [0|1|reset|log|lang en|ru|first|color <argb>|nudge <control> <dx> <dy>|hide <control>|nudge off|demo <addr> <port> <shown port>|demo off|widgets] - co-op window state (open/close it; the rest: tests, README pictures)", c_uistate, nullptr,
                   SK_CMD_OUTSIDE_FRAME);
    H->command_add(M, "coop.fov",
                   "coop.fov [0-6|reset|ray <x> <y>] - wider third-person FOV in Creature and Adventure (BUG-0866)",
                   fov::command, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace coop_core
