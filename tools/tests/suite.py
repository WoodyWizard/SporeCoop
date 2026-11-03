"""Every automated test with its properties (used by tools/lab.py and tools/tests/run_all.sh).

  name      result logs: out/lab/<run>/<name>.log
  cmd       command line, run from the repository root with SPORE_LANE set
  suites    smoke (before a merge, ~10 min on 3 lanes), full (every test), nightly (full + long),
            extended (paths outside the other suites: 3-4 players, hot reload; also nightly),
            multi (3-4 players per stage in minutes, coop_party.py; also extended / nightly),
            campaign (a whole game of four bot players, coop_campaign.py: in no other suite, run it alone)
            mixed (the previous release and this build in one session, coop_mixed_version.py: in no other suite;
            it uses the neighbour lane too, run it alone: `lab.py run mixed --lane 9` with lane 10 idle)
            players (generated variants of chosen tests under the conditions of real players: the guest
            at 15 fps, a mobile / bursty network, both, four players; docs/06-testing.md#testing-degraded)
  warm      may reuse running games of the lane (twoinst.start with SPORE_WARM=1: the fixture is
            loaded into a fresh save slot, the guest rejoins; no restart). Tests that change the
            profile, enter editors or start their own games are cold.
  exclusive needs the machine alone (LAN discovery in the UI test sees other lanes' hosts)
  minutes   rough length, the scheduler starts long tests first
  timeout   seconds before the lab kills the test (default lab.TIMEOUT, 30 min)
  gates     the log gates of the test (tools/loggates.py, docs/06-testing.md#testing-log-gates): None = the
            defaults (sync-check MISMATCH, hitch over 2 s in play, mem low, address space loss slope);
            a dict changes a threshold ({'hitch_ms': 5000}) or switches gates off ({'off': ('hitch',)});
            NO_GAME for tests that run no game (their lane's idle games are not theirs)
  monitor   the invariant monitor's settings for this test (tools/tests/monitor.py DEFAULTS overrides, e.g.
            {'census_period': 60} to poll coop.census; 'off': no monitor), passed as SPORE_MONITOR
  allow     log gate findings this test may have, each allow(gate, ticket, reason, ...) with a ticket
            id and a reason: printed as "allowed" in the result, never silent
  uses_free_lane  the test plays on SPORE_FREE_LANE (outside the run), takes that lane's lock
            (<lab folder>/.update_check-lane<N>.lock) and prints `LAB-WATCH lane=<N> since=<unix time>`
            once it holds it: the lab watches that lane for crash / hang reports and dead games from
            then on, and cleans it after an interrupt only when the lock is free (BUG-0249, BUG-0250)
"""

T = []
NO_GAME = {'off': ('sync-check', 'hitch', 'mem-low', 'as-slope', 'monitor')}


def allow(gate, ticket, reason, stage=None, kind=None, who=None, match=None):
    """a log gate finding a test may have (tools/loggates.py): gate sync-check | hitch | mem-low |
    as-slope; ticket BUG-NNNN (the bug that makes it, open until fixed); reason: why it is not this
    test's failure. stage / kind / who / match narrow it ('Tribe', 'p apart', 'guest', a regex on the
    log line). tools/tests/lab_selftest.py checks that the ticket exists and the reason is given."""
    return dict(gate=gate, ticket=ticket, reason=reason, stage=stage, kind=kind, who=who, match=match)


# allowed in every test (each a known bug with its ticket; empty when every gate holds everywhere)
ALLOW_ALL = [
]


def add(name, cmd, suites=('full',), warm=False, exclusive=False, minutes=4, fixture=None, reports=False,
        uses_free_lane=False, timeout=None, gates=None, allow=(), monitor=None):
    """reports=True: the test makes crash / hang reports or faults on purpose and checks them itself;
    for every other test the lab fails it on a new crash-*/report-* file of its lane, a game that
    dies right after it, or a non-zero fault count in its log (tools/lab.py run_one, BUG-0036/0042)"""
    if 'campaign' in suites or 'mixed' in suites:  # hours on one lane / a second lane: only when asked for
        suites = set(suites)
    else:
        suites = set(suites) | ({'nightly'} if {'nightly', 'extended'} & set(suites) else {'full', 'nightly'})
    T.append(dict(name=name, cmd=cmd, suites=suites, warm=warm,
                  exclusive=exclusive, minutes=minutes, fixture=fixture, reports=reports,
                  uses_free_lane=uses_free_lane, timeout=timeout, gates=gates, allow=list(allow),
                  monitor=monitor))


P = 'python3 tools/tests/'
add('unit', 'make test', ('smoke',), minutes=1, gates=NO_GAME)
add('win_unit', P + 'win_unit.py', minutes=1, gates=NO_GAME)  # Win32 file handling under Wine: updater backups / pending list / duplicate files, received world slots (BUG-0035/0087/0089/0105/0114/0150; --old fails), the setup's Spore-without-GA folder and GitHub API 403 fallback (BUG-0391: SPORE_WIN_UNIT_ARGS=--old-ga fails; BUG-0392: SPORE_FIX_OFF=update.web_fallback fails)
add('creature', P + 'coop_two_instances.py --guest-menu', ('smoke',), minutes=4)
add('cell', P + 'coop_mode.py --fixture cell', minutes=3)
add('tribe', P + 'coop_mode.py --fixture tribe', minutes=3)
add('civ', P + 'coop_mode.py --fixture civ', minutes=3)
add('space', P + 'coop_mode.py --fixture space', minutes=3)
add('phase', P + 'coop_phase.py', minutes=6)
add('ui', P + 'coop_ui.py', ('smoke',), exclusive=True, minutes=5)
add('team', P + 'coop_team.py', ('smoke',), warm=True, minutes=3, fixture='creature')
add('tribe_coop', P + 'coop_tribe.py', ('smoke',), warm=True, minutes=4, fixture='tribe')
add('civ_coop', P + 'coop_civ.py', ('smoke',), warm=True, minutes=3, fixture='civ')
add('cell_coop', P + 'coop_cell.py', ('smoke',), warm=True, minutes=4, fixture='cell')
add('cell_meat', P + 'coop_cell_meat.py', warm=True, minutes=4, fixture='cell')  # kills, their meat, eating once
add('cell_spin', P + 'coop_cell_spin.py', warm=True, minutes=4, fixture='cell')  # creatures face as on the host on the guest; guest bites kill
add('cell_leave', P + 'coop_cell_leave.py', minutes=4)  # Cell -> Creature with mirrors on the guest, then the session ends
add('cell_sync', P + 'coop_cell_sync.py', minutes=8, fixture='cell')  # mirror map, missed totals, regrowth over a level line, knockout and leave (BUG-0088, 0128, 0041, 0070, 0061)
add('cell_net', 'SPORE_NET="delay=250,loss=0.05" ' + P + 'coop_cell_net.py', minutes=8, fixture='cell')  # lost state chunks, food race over a level line (BUG-0032, 0051)
add('cell_guest_bite', P + 'coop_cell_guest_bite.py', warm=True, minutes=4, fixture='cell')  # a guest's bite passes DamageCell's gate (BUG-0078; SPORE_CELL_FIX=0 fails)
add('cell_rescue', P + 'coop_cell_rescue.py', warm=True, minutes=3, fixture='cell')  # a knocked-out partner rescues nobody (BUG-0131; SPORE_CELL_FIX=0 fails)
add('cell_ko_gap', P + 'coop_cell_ko_gap.py', warm=True, minutes=3, fixture='cell')  # a knockout lasts through a late refresh (BUG-0238; SPORE_FIX_OFF=pool.ko_grace fails)
add('cell_food_away', P + 'coop_cell_food_away.py', warm=True, minutes=3, fixture='cell')  # a guest's meal while the host is away is acknowledged (BUG-0239; SPORE_FIX_OFF=pool.food_ack_away fails)
add('cell_away', P + 'coop_cell_away.py', warm=True, minutes=5, fixture='cell')  # the guest plays its own pool while the host is in the menu, the host's again after (BUG-0205; SPORE_FIX_OFF=pool.away fails)
add('cell_away_editor', P + 'coop_cell_away.py --editor', minutes=5)  # the same while the host is in its own cell editor (BUG-0205; SPORE_FIX_OFF=pool.away fails)
add('cell_reload_join', P + 'coop_cell_reload_join.py', warm=True, minutes=4, fixture='cell')  # after a world reload the guest's cell is next to the host (BUG-0352; SPORE_FIX_OFF=cell.join_after_load fails)
add('cell_bite_ring', P + 'coop_cell_bite_ring.py', warm=True, minutes=3, fixture='cell')  # a guest's bite shows the host creature's health ring in the guest's game (BUG-0350; SPORE_FIX_OFF=cell.bite_ring fails)
add('cell_respawn', P + 'coop_cell_respawn.py', warm=True, minutes=3, fixture='cell')  # the game's death and rebuild with the partner knocked out, both play on (BUG-0221)
add('diag_cell', P + 'coop_diag.py --fixture cell', warm=True, minutes=3, fixture='cell')  # session clock, /bug marks on both games, merge_logs, sync-check OK and a forced mismatch, its cost
add('diag_creature', P + 'coop_diag.py --fixture creature', warm=True, minutes=3, fixture='creature')  # the same in Creature
add('report_shot', P + 'coop_report_shot.py', warm=True, minutes=3, fixture='creature', reports=True)  # a /bug mark and "Report a problem" put a screenshot of the game (JPEG, <= 1280 wide, not black, no hitch) into the report archive (BUG-0442; --off or SPORE_REPORT_SHOT=0, [report] screenshot=0 in coop.ini, fails)
add('cell_respawn_swap', P + 'coop_cell_respawn.py --swap', warm=True, minutes=3, fixture='cell')  # the same with the guest's pool rebuilt (BUG-0221)
add('cell_editor_timed', P + 'coop_cell_editor_timed.py', minutes=5)  # the own cell editor entered within the 3 s after a real respawn, vulnerable after it (BUG-0237)
add('cell_editor_timed_revive', P + 'coop_cell_editor_timed.py --revive', minutes=5)  # the same within the 2 s after a revive (BUG-0237)
add('cell_editor_invuln', P + 'coop_cell_editor_invuln.py', minutes=5)  # invulnerable into the own cell editor, vulnerable after it (BUG-0237)
add('cell_spawn_guard', P + 'coop_cell_spawn_guard.py', warm=True, minutes=3, fixture='cell')  # coop.cell spawn with a slot that is not a live cell: "no such cell", no fault (BUG-0093)
add('dna_resend', P + 'coop_dna_resend.py', warm=True, minutes=3, fixture='creature')  # the DNA total reaches a guest that missed it (BUG-0128)
add('leave_cleanup', P + 'coop_leave_cleanup.py', minutes=4, fixture='space')  # every session end cleans every stage module (BUG-0033): Space mirrors after Leave, host while connected
add('space_coop', P + 'coop_space.py', ('smoke',), warm=True, minutes=2, fixture='space')
add('stage_switch', P + 'coop_stage_switch.py', minutes=13)  # every stage pair that matters, in game
add('vote', P + 'coop_vote.py', minutes=5)
add('vote_caption', P + 'coop_vote_caption.py', minutes=4)  # a guest's I'm ready counts while a caption holds the input lock (BUG-0273; SPORE_FIX_OFF=core.ui_locked_clicks fails)
add('vote_during_cinematic', P + 'coop_vote_cinematic.py', warm=True, minutes=10, fixture='creature')  # a vote that ends while a cinematic plays or the game time stands: the host's editor and the guest's own editor open after it, with their buttons (BUG-0404; SPORE_FIX_OFF=core.editor_cinematic and core.own_editor_busy fail)
add('busy_gate', P + 'coop_vote_cinematic.py --part gate', warm=True, minutes=7, fixture='creature')  # an action vote's result waits at the busy gate (a cinematic, the game time paused) and runs after it; a share waits for a cinematic, not a pause (BUG-0404; SPORE_FIX_OFF=core.vote_action_busy fails)
add('vote_stale', P + 'coop_vote_stale.py', minutes=5)  # a vote ends with its session and its stage (BUG-0043)
add('vote_end_editor', P + 'coop_vote_drop_editor.py --case 1', minutes=3)  # a guest's own editor due from a vote does not open after a Leave (BUG-0234; --nofix fails)
add('vote_drop_editor', P + 'coop_vote_drop_editor.py --case 2', minutes=3)  # a cancelled evolution vote opens no guest editor (BUG-0234; --nofix fails)
add('world_queue', P + 'coop_world_queue.py', minutes=6)  # one save for shares waiting at once, shares of gone players dropped, no load inside an editor, no host world after Leave (BUG-0112/0166/0157/0074; --without all fails)
add('join_other_world', P + 'coop_join_other_world.py', minutes=5)  # a guest in another world of the stage gets no DNA / NPC removal from the host (BUG-0130, BUG-0231 DNA row 2; --without-fix and SPORE_FIX_OFF=core.world_dna fail)
add('civ_other_world', P + 'coop_civ_other_world.py', minutes=5)  # a guest in another Civ world gets no host vehicle / city state / money total applied (BUG-0231; --nofix and SPORE_FIX_OFF=res.elsewhere fail)
add('tribe_other_world', P + 'coop_tribe_other_world.py', minutes=5)  # a guest in another Tribe world walks its own members (BUG-0231; --nofix fails)
add('share_retry', P + 'coop_share_retry.py', minutes=5)  # a world share that failed after the save is retried (BUG-0231; SPORE_FIX_OFF=core.share_retry fails)
add('leave_during_share', P + 'coop_leave_during_share.py', minutes=4)  # Leave right after a share save: the host can still leave the stage (BUG-0140)
add('exit_thread', P + 'coop_exit_thread.py', minutes=2, reports=True)  # ExitProcess without Shutdown: no abort from the keepalive std::thread (BUG-0107)
add('adapters', P + 'coop_adapters.py', minutes=5, reports=True)  # BUG-0286: two games of one bottle look up their local addresses (GetAdaptersAddresses, crashed in Wine's nsi.dll) 900 times at the same moment, then the guest leaves and a lookup faults: both games keep running (SPORE_FIX_OFF=core.adapters fails)
add('fix_registry', P + 'coop_fix_registry.py', minutes=2)  # coop.fix lists every fix switch, all off / on round-trips, the old commands share the bools (BUG-0116)
add('ui_status', P + 'coop_ui_status.py', minutes=7)  # the co-op window: port check, a pasted address, the status after the join and the session end, LAN pages, no "host is back" after a disconnect (BUG-0138, BUG-0104, BUG-0183, BUG-0085; --without-fix fails)
add('join_name', P + 'coop_join_name.py', warm=True, minutes=4)  # join by host name with a slow name server: the frames go on, the guest gets in (BUG-0204; --without-fix fails)
add('ui_menu_leave', P + 'coop_ui_menu_leave.py', minutes=2)  # the galaxy menu's Co-op item is dropped as the game leaves the menu (BUG-0163)
add('reload_update', P + 'coop_reload_update.py', minutes=2)  # hot reload while the update check waits: the old thread stays in the old image (BUG-0060)
add('update_leak', P + 'coop_update_leak.py', minutes=4)  # 5 refused update attempts free their 20 MB packs (BUG-0241; SPORE_FIX_OFF=core.update_unwind fails)
add('guest_editor', P + 'coop_guest_editor.py', minutes=4)
add('guest_editor_locks', P + 'coop_guest_editor_locks.py', minutes=4)  # the guest's own editor locks the parts the species has not collected (BUG-0368; SPORE_FIX_OFF=creations.own_editor_items fails)
add('parts_shared', P + 'coop_parts_shared.py', minutes=6, fixture='creature')  # the parts a player finds are the party's: host -> guest, guest -> host, the guest's editor palette, a rejoin (BUG-0401; SPORE_FIX_OFF=parts.shared fails)
add('parts_transition', P + 'coop_parts_shared.py --from-cell', minutes=10, fixture='cell')  # BUG-0441: after Cell -> Creature the players' way the guest's collected parts are the host's within 10 s (the Cell twins), then parts_shared's finds, editor (the palette offers the host's parts) and rejoin (SPORE_FIX_OFF=parts.census_states fails)
add('old_species', P + 'coop_old_species.py', minutes=3)  # an NPC of a player's replaced creature shows on the guest: the host answers its species (BUG-0370; SPORE_FIX_OFF=creations.old_species fails)
add('dna_notice', P + 'coop_dna_notice.py', minutes=3)  # shared DNA names its cause on the partner's screen (+N DNA from Bob's hunt) and in the logs (dna: a -> b cause=...); a feature, no switch
add('dna_notice_cell', P + 'coop_dna_notice.py --cell', minutes=3)  # Cell: a partner's meals named on the other's screen (+10 food (DNA) from Bob's meals) and in the logs
add('notify', P + 'coop_notify.py', minutes=3)
add('window_layout', P + 'coop_window_layout.py', minutes=3)  # the co-op window re-centred after a smaller resolution, the status block never over the window or the panels (BUG-0113/0155; --old fails)
add('sync', P + 'coop_sync.py', warm=True, minutes=6)
add('lan_dedup', P + 'coop_lan_dedup.py', warm=True, minutes=3, fixture='creature')  # a host on two network addresses is one row in the guest's LAN list (BUG-0210; SPORE_FIX_OFF=net.lan_dedup fails)
add('sync_lowfps', P + 'coop_sync.py --fps 15 civ tribe', warm=True, minutes=5)  # the guest at 15 fps: vehicles, citizens and animals within the sync limits (BUG-0175 regression; the fix's FAIL/PASS is npc_tests)
add('creations', P + 'coop_creations.py', minutes=4)
add('net_robust', P + 'coop_net_robust.py', minutes=4)  # malformed messages from a joined player (BUG-0201)
add('chat_long', P + 'coop_chat_long.py', warm=True, minutes=2, fixture='creature')  # a reliable message bigger than a packet is split, nothing behind it stalls (BUG-0034)
add('creations_cell', P + 'coop_creations_cell.py', minutes=4)
add('cell_own_load', P + 'coop_cell_own_load.py', minutes=4)  # BUG-0342
add('host_old_choice', P + 'coop_host_old_choice.py', minutes=3)  # BUG-0344
# BUG-0341: Carol (no Creepy & Cute pack) on a lane outside the run (SPORE_FREE_LANE), host and Bob on the run's lane
add('guest_missing_pack', P + 'coop_guest_missing_pack.py', minutes=6, uses_free_lane=True)
add('guest_pack_paints', P + 'coop_guest_pack_paints.py', minutes=4)  # a guest without Creepy & Cute sees the host's pack paints as base game paints (BUG-0369; SPORE_FIX_OFF=creations.paint_substitute fails)
add('unknown_parts', P + 'coop_unknown_parts.py', minutes=6, uses_free_lane=True)  # a guest without a parts pack (Spore_Pack_03) does not crash on a part it lacks and logs the id (BUG-0452; SPORE_FIX_OFF=creations.unknown_parts fails)
add('content_civ', P + 'coop_content_civ.py', minutes=10, uses_free_lane=True)  # a civ mirror vehicle whose design model names a part the guest's game lacks does not fault and is shown, and the save it gets into reloads without the mod's key (BUG-0469; SPORE_FIX_OFF=content.all_kinds / content.model_key fail)
add('content_space', P + 'coop_content_space.py', minutes=6, uses_free_lane=True)  # a host ship whose model names a part the guest's game lacks does not fault and is mirrored (BUG-0469; SPORE_FIX_OFF=content.all_kinds fails)
add('creations_rejoin', P + 'coop_creations_rejoin.py', minutes=5)  # a newcomer under a reused id is not shown with the previous player's creature (BUG-0076)
add('editor_desync', P + 'coop_editor_desync.py', minutes=7)  # mating call and editors in Cell and Creature, real input (BUG-0411: --old-wait fails about 1 run in 2)
add('creature_desync', P + 'coop_creature_desync.py', minutes=4)  # editors, a crash and a reload: companion of a host-only species, no NPC copies, kills, the guest's look
add('editor_strays', P + 'coop_editor_strays.py', minutes=6)  # two evolutions: no puppet left over, herd homes on the surface (crash 0xB88BEA)
add('creature_e84', P + 'coop_creature_e84.py', minutes=5)  # a save with the partner's puppet, loaded after a restart: no crash on attack (0xD48634), removed
add('save_hygiene', P + 'coop_save_hygiene.py', minutes=5)  # a save made with the partner's puppet holds no puppet, no herd of ours after a restart (BUG-0020; SPORE_SAVE_FIX=0 fails)
add('save_hygiene_leave', P + 'coop_save_hygiene.py --leave', minutes=5)  # the partner left before the save: its empty herd is not saved either (BUG-0255; SPORE_FIX_OFF=savestrip.empty_herds fails)
add('alt_enter', P + 'coop_alt_enter.py', minutes=4)  # Alt+Enter / Alt+Shift+Enter in co-op: no fault at 0x7777BE (device recreation), on the ground
add('nest_species', P + 'coop_nest_species.py', minutes=4)  # the guest owns another creation under the key of the host's: the host's species at its nests, its table
add('migration', P + 'coop_migration.py', minutes=5)  # brain upgrade, editors, migration; both claim the new nest: no fault on the guest (0xC6AE60)
add('select', P + 'coop_select.py', ('smoke',), warm=True, minutes=3, fixture='creature')
add('social_console', P + 'coop_social_console.py', warm=True, minutes=3, fixture='creature')  # digits typed in the open cheat console play no social ability (BUG-0149; --without-fix fails)
add('creature_kills', P + 'coop_creature_kills.py', minutes=3)  # a guest's kill dies on the guest too (no 0 hp mirror standing), NPCs notice the partner
add('creature_death', P + 'coop_creature_death.py', minutes=4)  # alone: die, skip the caption and the respawn cutscene
add('creature_respawn', P + 'coop_creature_respawn.py', minutes=5)  # both players die (also after a pack rescue): the hatchling grows up and walks
add('diag', P + 'diag_check.py --crash', minutes=5, reports=True)  # kept logs, environment, version in the menu, report button, hang / crash reports
add('kit_freeze', P + 'kit_freeze.py', minutes=4, reports=True)  # stack / hangdump under a held heap, cheat-console waits, faults inside the log / fault logger, hook churn, hooks made while threads run the function, new hooks' originals (BUG-0050/0065/0115/0132/0202/0398; --old: every case fails)
add('kit_hooknew', P + 'kit_freeze.py --case hooknew', minutes=2, reports=True)  # new hooks made while threads call the functions: no detour runs before its original is set (BUG-0398, the guest's call to address 0; SPORE_FIX_OFF=hookorig fails)
add('kit_docs', P + 'kit_profile_docs.py', minutes=2)  # a test profile's My Spore Creations is in the profile, the bottle's own Documents unchanged (BUG-0156; --old fails)
add('kit_crash', P + 'kit_crash.py', minutes=7, reports=True)  # crash reports for a stack overflow and a failed archive (out of memory); old reports pruned; the exception line names the crashed thread (BUG-0056/0127/0137/0248; --old: every case fails)
# frames from the game loop when Present is lost (issue #4, the Steam overlay on Windows)
add('frames_loop', P + 'coop_frames_loop.py', ('smoke',), warm=True, minutes=2, fixture='creature')
add('exit_menu', P + 'coop_exit_menu.py', minutes=3)  # a guest exits to the main menu and comes back
add('host_away_kill', P + 'coop_host_away_kill.py', minutes=4)  # BUG-0082: the guest's kill list does not outlive the host's absence
add('npc_reuse', P + 'coop_npc_reuse.py', minutes=3)  # BUG-0044 / BUG-0023: a reused NPC object gets a new id (the guest's mirror is alive), ids never repeat
add('cinematic_puppet', P + 'coop_cinematic_puppet.py', minutes=3)  # BUG-0066: a puppet removed during a cinematic stays a puppet (no host NPC id)
add('npc_pooled', P + 'coop_npc_pooled.py', minutes=3)  # BUG-0274: no guest proxy of an NPC the host's game put back into its pool
add('npc_far', P + 'coop_npc_far.py', minutes=4)  # BUG-0280: a guest 300 m from the host keeps the NPCs around it, no nest member flickers (SPORE_FIX_OFF=npc.view_centres fails)
add('rejoin_saved_puppet', P + 'coop_rejoin_saved_puppet.py', minutes=6)  # BUG-0256: a guest that rejoins after a passed vote and loads the host's world does not fault in Herd_Update (SPORE_FIX_OFF=npc.herd_nulls fails)
add('tribe_far', P + 'coop_tribe_far.py', minutes=4, fixture='tribe')  # BUG-0280 (Tribe): the host's game keeps herds with animals where a guest points 300-450 m from the host (SPORE_FIX_OFF=npc.view_centres_tribe fails; SPORE_FIX_OFF=npc.rts_views fails its BUG-0291 check)
add('npc_swap', P + 'coop_npc_swap.py', minutes=3)  # BUG-0110: an NPC tick that does not return leaves no puppet as the game's avatar
add('world_reload', P + 'coop_world_reload.py', minutes=6)  # BUG-0119: after a reload the guest plays its own creature again although the avatar's address came back (--without-fix fails)
add('leave_avatar', P + 'coop_leave_avatar.py', minutes=7)  # BUG-0264: a guest leaving the Creature stage for a shared world never uses the avatar the game tears down (no FAIL run: with the switch world.leave_avatar off it passed 5 times, lab 20261029-085208 / 085452 / 085805-fix-w6-avatar; BUG-0264 no-fail-run, not reproduced with 2 players)
add('leave_avatar_cell', P + 'coop_leave_avatar.py --cell --rounds 3', minutes=8)  # BUG-0264: the campaign's way: Cell -> Creature, the host's first editor, the shares after it
add('cell_evolve_poll', P + 'coop_cell_evolve_poll.py', minutes=6, fixture='cell')  # BUG-0271: the host ends the Cell stage while the consoles keep asking (coop.status / avatar / sync): no cell read from the freed pool (SPORE_FIX_OFF=core.stale_local fails)
add('guest_editor_hold', P + 'coop_guest_editor_hold.py', suites=('extended',), minutes=9, fixture='cell')  # BUG-0283 (not reproduced in 3 runs): the host's new body reaches a guest while its own editor is open; one red cross click must leave the unchanged editor
add('legless_guest', P + 'coop_legless_guest.py --guest new', minutes=10, fixture='cell')  # BUG-0278: a new guest (no own creations) adds legs in the own editor of the first Creature vote and picks Save New: it plays that creature on both screens (not the host's legless pre-editor one under the same key), after a rejoin too, changes it in its next editor and goes back to the world's own (SPORE_FIX_OFF=creations.pre_editor fails)
add('legless_guest_replace', P + 'coop_legless_guest.py --guest replace', ('extended',), minutes=10, fixture='cell')  # BUG-0278: the same with Replace: the return from the editor no longer pins the host's legless creature (SPORE_FIX_OFF=creations.pre_editor fails)
add('legless_guest_done', P + 'coop_legless_guest.py --guest done', ('extended',), minutes=10, fixture='cell')  # BUG-0278: the same guest clicks done without a change: it plays the host's evolved species
add('legless_guest_cancel', P + 'coop_legless_guest.py --guest cancel', ('extended',), minutes=10, fixture='cell')  # BUG-0278: the same guest cancels (and confirms Quit): it plays the host's evolved species
add('legless_guest_dontsave', P + 'coop_legless_guest.py --guest dontsave', ('extended',), minutes=10, fixture='cell')  # BUG-0278: the same guest adds legs and picks Don't Save: it plays the host's evolved species
add('npc_rejoin_paused', P + 'coop_npc_rejoin_paused.py', minutes=4)  # BUG-0125: a guest that rejoins while the host is paused gets the NPCs around it (--without-fix fails)
add('evolve_world', P + 'coop_evolve_world.py', minutes=6)  # BUG-0019: the world after an evolution that starts a migration waits for its cinematic: the guest grows up and migrates (--without-fix fails)
add('creation_leave', P + 'coop_creation_leave.py', minutes=5)  # BUG-0055: after leaving, the guest's own creation under the host's key is its own again
add('anim_teardown', P + 'coop_anim_teardown.py', minutes=8)  # BUG-0091: no animation on a puppet freed by a world teardown
add('join_new_game', P + 'coop_join_new_game.py', minutes=5)  # the host starts a new game (typed planet name) next to an old one: the guest gets the new one
add('cell_new_game', P + 'coop_cell_new_game.py --diet carn --early --minutes 3', minutes=10)  # two new players (empty profiles), a new Cell game from the stage wizard, the guest joins in the intro: both eat (BUG-0279)
add('cell_far_guest', P + 'coop_cell_new_game.py --diet carn --minutes 0 --probe 0 --apart 70 --far-probe 4', minutes=10)  # a guest far from the host finds food, eats it, the host counts it (BUG-0174, BUG-0279; SPORE_FIX_OFF=cell.partner_tiles fails)
add('tribe_play', P + 'coop_tribe_play.py', ('smoke',), warm=True, minutes=3, fixture='tribe')
add('civ_play', P + 'coop_civ_play.py', warm=True, minutes=3, fixture='civ_npc')
add('civ_reload', P + 'coop_civ_reload.py', warm=True, minutes=5, fixture='civ_npc')  # tables after a load and a leave (BUG-0040, BUG-0059; --nofix fails)
add('menu_stale_player', P + 'coop_menu_stale_player.py', minutes=12, fixture='tribe')  # a load from the galaxy menu with the old world's player empire left there (BUG-0284; --nofix fails)
add('tribe_leave_walk', P + 'coop_tribe_leave_walk.py', minutes=10, fixture='tribe')  # a cleaned-up citizen sent walking as Tribe is left for the menu, then the next load (BUG-0310, BUG-0285; --nofix fails)
add('tribe_chiefs_apart', P + 'coop_tribe_chiefs_apart.py', warm=True, minutes=3, fixture='tribe')  # the players' chiefs do not stand on each other, and they can walk (BUG-0340; --nofix fails)
add('tribe_rejoin_npcs', P + 'coop_tribe_rejoin_npcs.py', minutes=6, fixture='tribe')  # a guest that rejoins in Tribe gets all the host's NPCs again (BUG-0328 scenario; the campaign's loss was not reproduced, no fix)
add('tribe_own_height', P + 'coop_tribe_own_height.py', warm=True, minutes=3, fixture='tribe')  # the guest's ground 5 m off the host's: members stand, no walking in place (BUG-0329; --nofix fails)
add('tribe_mirror_ground', P + 'coop_tribe_mirror_ground.py', warm=True, minutes=4, fixture='tribe')  # the guest's own member walked up and down a slope and new mirrors made while the guest's game time is paused (as for 10-40 s after a cold join): they stay on the guest's ground (BUG-0444; SPORE_FIX_OFF=tribe.mirror_ground fails)
add('physics_orphan', P + 'coop_physics_orphan.py', warm=True, minutes=3, fixture='tribe')  # a physics body without an owner on the guest in Tribe: the physics update does not fault in every frame (BUG-0449; SPORE_FIX_OFF=game.physics_owner fails)
add('civ_null_local', P + 'coop_civ_null_local.py', warm=True, minutes=4, fixture='civ')  # a frame without the city anchor keeps vehicles and owners (BUG-0223; --nofix fails)
add('civ_shared', P + 'coop_civ_shared.py', warm=True, minutes=4, fixture='civ')  # crossing spends, offline money, roles on a rejoin, a spend at a join (BUG-0106, BUG-0049, BUG-0064, BUG-0220; --nofix fails)
add('civ_city_join', P + 'coop_civ_city_join.py', warm=True, minutes=5, fixture='civ_npc')  # an NPC city founded while the guest joins (BUG-0120; --nofix fails)
add('civ_city_burst', P + 'coop_civ_city_burst.py', warm=True, minutes=3, fixture='civ_npc')  # three NPC cities the guest missed reach it at once with their vehicles (BUG-0380; SPORE_FIX_OFF=civ.city_burst fails)
add('civ_cursor', P + 'coop_civ_cursor.py', warm=True, minutes=3, fixture='civ')  # the sync-check leaves out a moving Civ pointer, a resting one is where the guest shows it (BUG-0380; SPORE_FIX_OFF=diag.rts_cursor fails)
add('civ_ai_spawn', P + 'coop_civ_ai_spawn.py', warm=True, minutes=4, fixture='civ_npc')  # the NPC AI spawns no vehicles on the guest (BUG-0188; --nofix fails)
add('civ_mirror_place', P + 'coop_civ_mirror_place.py', warm=True, minutes=3, fixture='civ_npc')  # the guest's game does not throw a mirrored vehicle off its place (BUG-0186; --nofix fails)
add('civ_ruin_repair', P + 'coop_civ_ruin_repair.py', warm=True, minutes=3, fixture='civ_npc')  # a ruin the host rebuilt does not stay on the guest (BUG-0324; --nofix fails)
add('civ_pause_city', P + 'coop_civ_pause_city.py', warm=True, minutes=3, fixture='civ_npc')  # a city founded while a guest is paused reaches it after the pause (BUG-0331)
add('civ_leave', P + 'coop_civ_leave.py', warm=True, minutes=4, fixture='civ')  # the vehicles of a guest who left go to a present player (BUG-0031; --nofix fails)
add('civ_sw_vote', P + 'coop_civ_sw_vote.py', warm=True, minutes=3, fixture='civ')  # a second superweapon request does not change the running vote (BUG-0117; --nofix fails)
add('civ_caption', P + 'coop_civ_caption.py', warm=True, minutes=3, fixture='civ_npc')  # the host's capture caption closes by itself with guests (BUG-0187; --nofix fails)
add('civ_away', P + 'coop_civ_away.py', warm=True, minutes=5, fixture='civ')  # the guest's vehicles drive while the host is in the menu (BUG-0151; --nofix fails)
add('tribe_away', P + 'coop_tribe_away.py', warm=True, minutes=5, fixture='tribe')  # the guest's members walk while the host is in the menu (BUG-0151; --nofix fails)
add('dna_away', P + 'coop_dna_away.py', warm=True, minutes=5, fixture='creature')  # guest DNA while the host has no creature reaches the pack (BUG-0151; --nofix fails)
add('away_editor', P + 'coop_away_editor.py', warm=True, minutes=6, fixture='creature')  # a guest's goal reward reaching the host in its editor is paid when it is back (BUG-0205; --nofix fails)
add('away_stages', P + 'coop_away_stages.py', warm=True, minutes=3, fixture='space')  # a host editor counts as away only in Creature/Tribe/Civ, not in Space; the menu still does (BUG-0233; --nofix fails)
add('tribe_chief_removed', P + 'coop_tribe_chief_removed.py', warm=True, minutes=3, fixture='tribe')  # a chief removed at once (BUG-0081; --nofix fails)
add('tribe_epic', P + 'coop_tribe_epic.py', warm=True, minutes=4, fixture='tribe')  # epic hitters, herd, save, chiefs after the session (BUG-0146, BUG-0121, BUG-0067; --nofix fails)
add('tribe_editor_world', P + 'coop_tribe_editor_world.py', warm=True, minutes=5, fixture='tribe')  # an editor visit keeps the co-op epic tracked, out of the share save (BUG-0217; --nofix fails)
add('tribe_snapshot', P + 'coop_tribe_snapshot.py', minutes=12)  # after the live Creature -> Tribe change the host's place is its planet, not snapshot.spo: the guest is together with it (BUG-0258; --nofix fails)
add('tribe_gameover', P + 'coop_tribe_gameover.py', minutes=4)  # a member added during the game-over caption (BUG-0172; --nofix fails)
add('tribe_ko', P + 'coop_tribe_ko.py', warm=True, minutes=3, fixture='tribe')  # a knocked-out NPC member lies down on the guest (BUG-0173; --nofix fails)
add('tribe_bounce_mirrors', P + 'coop_tribe_bounce_mirrors.py', warm=True, minutes=4, fixture='tribe')  # the guests mirror the host's tribe after its game over and bounce back (BUG-0288; --nofix fails)
add('tribe_act', P + 'coop_tribe_act.py', warm=True, minutes=4, fixture='tribe')  # the coop.tribe act test command does not fault the host (BUG-0260; --nofix fails)
add('tribe_walk', P + 'coop_tribe_walk.py', warm=True, minutes=5, fixture='tribe')  # the players' tribe members walked by box-select + right-click, sampled every frame on both games: the guest's mirrors walk with the host's, no steps or jumps (BUG-0365; SPORE_FIX_OFF=tribe.mirror_walk fails)
add('tribe_walk_mobile', 'SPORE_NET=mobile ' + P + 'coop_tribe_walk.py --fps 30', ('nightly',), minutes=5, fixture='tribe')  # tribe_walk over a mobile network (120 ms, 60 ms jitter, 3 % loss), both games at 30 fps (BUG-0365)
add('tribe_npc_form', P + 'coop_tribe_npc_form.py', minutes=4, fixture='tribe')  # BUG-0381: an NPC tribe the host's game forms during play is on the guest with all its members, same species (SPORE_FIX_OFF=tribe.npc_tribes fails)
add('tribe_npc_dash', P + 'coop_tribe_npc_dash.py', warm=True, minutes=4, fixture='tribe')  # two animals run side by side on the host: the guest shows them on their path (BUG-0305; SPORE_FIX_OFF=npc.trail fails)
add('creature_npc_dash', 'DASH_FIXTURE=creature ' + P + 'coop_tribe_npc_dash.py', minutes=4, fixture='creature')  # BUG-0281: two nest NPCs run side by side at 4 m/s on the host, the guest (15 fps) stands beside the path's end: its proxies stay on the path (SPORE_FIX_OFF=npc.kinematic fails)
add('puppet_walk', P + 'coop_puppet_walk.py', warm=True, minutes=4, fixture='creature')  # BUG-0360: each player walks by WASD and clicks, the other's game samples the puppet every frame: no jumps, it moves and walks (SPORE_FIX_OFF=creature.puppet_walk fails)
add('npc_walk', P + 'coop_npc_walk.py', warm=True, minutes=4, fixture='creature')  # BUG-0361: the host's NPCs wander, walk and run (MoveTo) and flee around the guest, its game samples their proxies every frame: no jumps, no steps, walk gait on screen (SPORE_FIX_OFF=npc.walk fails)
add('npc_walk_slope', P + 'coop_npc_walk.py --goto=-195,-465,-45 --falls 3', warm=True, minutes=4, fixture='creature')  # BUG-0366 for NPC proxies: the host's NPCs walked on a slope of the creature fixture; the guest's walked proxies start to fall at most 3 per 100 s more often than the host's own NPCs
add('npc_walk_tribe', 'TRACK_FIXTURE=tribe ' + P + 'coop_npc_walk.py', warm=True, minutes=4, fixture='tribe')  # BUG-0361 (Tribe): the same around the guest's tribe (SPORE_FIX_OFF=npc.walk_tribe fails)
add('puppet_slope', 'SPORE_NET=player ' + P + 'coop_puppet_walk.py --dir guest --guest-fps 30 --goto=-195,-465,-45 --secs 40 --ground', minutes=4, fixture='creature')  # BUG-0366: the guest at 30 fps over a home connection (spikes, loss bursts) walks on a slope of the fixture: its puppet on the host never starts to fall ("constantly jumping"; SPORE_FIX_OFF=creature.puppet_ground fails)
add('puppet_under', 'PUPPET_WALK_CMDS="coop.track lift -1" ' + P + 'coop_puppet_walk.py --dir guest --secs 25 --stand 8 --ground', minutes=4, fixture='creature')  # BUG-0367: the guest's states 1 m under the host's ground (coop.track lift -1): its puppet is never drawn under the ground (SPORE_FIX_OFF=creature.puppet_ground fails)
# the walks at the players' frame rate (the game caps at 30 fps) and over the `player` network (BUG-0394)
add('puppet_walk_fps30', 'SPORE_FPS=30 ' + P + 'coop_puppet_walk.py', minutes=5, fixture='creature')  # puppet_walk with both games at 30 fps (BUG-0394)
add('puppet_walk_player', 'SPORE_FPS=30 SPORE_NET=player ' + P + 'coop_puppet_walk.py', minutes=5, fixture='creature')  # puppet_walk at 30 fps over the player network (BUG-0394)
add('puppet_walk_novel', 'SPORE_FPS=30 PUPPET_WALK_CMDS="coop.track novel 1" ' + P + 'coop_puppet_walk.py', minutes=5, fixture='creature')  # BUG-0394: each game takes the partner's states without their speed (a move carried by an animation reports none): the puppet still walks with it (SPORE_FIX_OFF=creature.puppet_path fails)
add('npc_walk_novel', 'SPORE_FPS=30 NPC_WALK_GUEST_CMDS="coop.npctrack novel 1" ' + P + 'coop_npc_walk.py', minutes=5, fixture='creature')  # BUG-0394: the guest takes the host's NPC states without their speed (moves carried by an animation report none): the proxies still walk (SPORE_FIX_OFF=npc.path fails)
add('npc_walk_fps30', 'SPORE_FPS=30 ' + P + 'coop_npc_walk.py', minutes=5, fixture='creature')  # npc_walk with both games at 30 fps (BUG-0394)
add('npc_walk_player', 'SPORE_FPS=30 SPORE_NET=player ' + P + 'coop_npc_walk.py', minutes=5, fixture='creature')  # npc_walk at 30 fps over the player network (BUG-0394)
add('tribe_walk_fps30', P + 'coop_tribe_walk.py --fps 30', minutes=5, fixture='tribe')  # tribe_walk with both games at 30 fps (BUG-0394; --fps: capped after the setup, as tribe_walk_mobile)
add('tribe_walk_player', 'SPORE_NET=player ' + P + 'coop_tribe_walk.py --fps 30', minutes=5, fixture='tribe')  # tribe_walk at 30 fps over the player network (BUG-0394)
add('npc_run_hp', P + 'coop_npc_run_hp.py', warm=True, minutes=3, fixture='creature')  # BUG-0334: a running NPC's health and death reach the guest (SPORE_FIX_OFF=npc.kinematic_sync fails)
add('creature_partner_hp', P + 'coop_creature_partner_hp.py', warm=True, minutes=3, fixture='creature')  # BUG-0373: each game shows the partner's real health and max health on its creature (SPORE_FIX_OFF=creature.partner_hp fails)
add('monitor_plant', P + 'coop_monitor_plant.py', minutes=5, fixture='creature', gates={'off': ('monitor', 'sync-check')})  # the invariant monitor catches one planted problem per rule over the mod's monitor fields (drawn, puppet_hp, posse_damage, unlocks, proxies; ground: puppet_under with SPORE_FIX_OFF=creature.puppet_ground; docs/06-testing.md#testing-monitor-fields); fails on a build without the fields
add('monitor_tribe', P + 'coop_monitor_tribe.py', minutes=5, fixture='tribe', gates={'off': ('monitor', 'sync-check')})  # the monitor in Tribe: no drawn violation for a pointer that jumped just before the mark (BUG-0416; SPORE_MONITOR='{"drawn_still": -1}' fails), drawn reported for a planted copy off a resting pointer, no link violation for two world transfers (BUG-0415)
add('creature_party', P + 'coop_creature_party.py', warm=True, minutes=3, fixture='creature')  # BUG-0374: the host's pack does not fight the guest after the guest's hit on it (SPORE_FIX_OFF=npc.party_peace fails)
add('nest_eggs', P + 'coop_nest_eggs.py', warm=True, minutes=3, fixture='creature')  # BUG-0384: the host's eggs at a nest of another species are on the guest at the same places, on the ground; an egg eaten on one game is eaten on the other (SPORE_FIX_OFF=nest.eggs fails)
add('nest_eggs_air', P + 'coop_nest_eggs_air.py', warm=True, minutes=3, fixture='creature')  # BUG-0443: an NPC egg that hangs in the air (2 m up) lies on the surface again on the host within 6 s; the other egg stays (SPORE_FIX_OFF=nest.egg_ground fails)
add('creature_brain_hp', P + 'coop_creature_brain_hp.py', minutes=4, fixture='creature')  # BUG-0375: a guest playing its own creature gets the brain level's health (SPORE_FIX_OFF=creature.own_brain_hp fails)
# w33 (audit items 15, 17, 19, 22, 26, 27): what the other player sees in fights, after events, of bodies, of the pack
add('corpse_positions', P + 'coop_npc_corpse.py', warm=True, minutes=6, fixture='creature')  # BUG-0383: bodies of NPCs killed standing, running, thrown, on a slope, by another NPC, while the guest was away, eaten: the guest shows each where the host has it, dead, on the ground at 3 s and 30 s (SPORE_FIX_OFF=npc.dead_proxy fails; SPORE_FIX_OFF=npc.play_death fails)
add('corpse_positions_player', 'SPORE_FPS=guest:30 SPORE_NET=player ' + P + 'coop_npc_corpse.py', minutes=7, fixture='creature')  # corpse_positions with the guest at 30 fps over the player network
add('team_real_damage', P + 'coop_team_real_damage.py', warm=True, minutes=4, fixture='creature')  # the pack's regen and rescue after an NPC's own bites (no coop.team hit): the other game shows the hurt, the regen, the rescued creature alive and the notice (SPORE_FIX_OFF=creature.partner_hp fails)
add('team_real_damage_player', 'SPORE_FPS=guest:30 SPORE_NET=player ' + P + 'coop_team_real_damage.py', minutes=5, fixture='creature')  # team_real_damage with the guest at 30 fps over the player network
add('puppet_fight', P + 'coop_puppet_fight.py', warm=True, minutes=4, fixture='creature')  # each player fights NPCs (clicks, ability keys, sprints, turns), the other's game samples its creature every frame: drawn jumps and teleports <= 1 per minute, drawn p95 <= 2.5 m off the fighter at the same moment while it moves, drawn path <= 0.3 s late (both creature.puppet_path and creature.puppet_walk off: FAIL, lab 20261101-141612)
add('puppet_fight_player', 'SPORE_FPS=guest:30 SPORE_NET=player ' + P + 'coop_puppet_fight.py', minutes=5, fixture='creature')  # puppet_fight with the guest at 30 fps over the player network (BUG-0414: steps out of the observer's view, a creature on a nest mound drawn on the plain terrain, are not judged)
add('puppet_after_events', P + 'coop_puppet_after_events.py', minutes=9, fixture='creature', gates={'hitch_ms': 5000})  # the partner's creature shown within 2 m after a rejoin, the vote's editors, deaths and respawns, Alt+Enter, cinematics, a world share (PUPPET_EVENTS_PLANT=hold fails)
add('puppet_after_events_player', 'SPORE_FPS=guest:30 SPORE_NET=player ' + P + 'coop_puppet_after_events.py', minutes=10, fixture='creature', gates={'hitch_ms': 5000})  # puppet_after_events with the guest at 30 fps over the player network (hitch gate 5 s: its Alt+Enter device reset took 2.3 s on the 30 fps guest)
add('creature_social_ally', P + 'coop_creature_social_ally.py', warm=True, minutes=5, fixture='creature')  # BUG-0021: the guest impresses 3 Kooks by real input: each impressed on both games, the species allied on both, the guest's card; an allied Kook recruited by the host follows it on both games (SOCIAL_ALLY_PLANT=npcsync fails; SPORE_FIX_OFF=social.no_bite fails in about half of the runs: the guest's key 1 kills a Kook)
add('creature_social_ally_player', 'SPORE_FPS=guest:30 SPORE_NET=player ' + P + 'coop_creature_social_ally.py', minutes=6, fixture='creature')  # creature_social_ally with the guest at 30 fps over the player network
add('creature_editor_soak', P + 'coop_editor_soak.py', ('extended',), minutes=30, timeout=3600, fixture='creature')  # BUG-0371: 32 rounds of vote, editors and done (a world share every 4): every round back, address space loss <= 8 MB per round, largest free block >= 128 MB, no fault (SOAK_PLANT_MB=40 fails)
add('away_refill', P + 'coop_away_refill.py', minutes=3)  # BUG-0180: the host leaves to the main menu: the guest's game has its own NPCs where the host's were at once (SPORE_FIX_OFF=npc.away_locals fails)
add('editor_after_swap', P + 'coop_editor_after_swap.py', minutes=5)  # BUG-0318: a 6.7 fps guest's editor right after a rejoin draws and can be left (SPORE_FIX_OFF=creations.editor_after_swap fails)
add('editor_after_load', P + 'coop_editor_after_swap.py --no-swap', minutes=5)  # BUG-0318: the same without an avatar swap (only the loaded world's new avatar)
add('ability_bar', P + 'coop_ability_bar.py', minutes=5)  # a guest's ability bar holds its own creature's abilities after the swap, keys do not fault (BUG-0358; SPORE_FIX_OFF=creations.ability_bar fails)
# two bots play Civilization for 10 minutes through real input (docs/stability/civ.md)
add('play_civ', P + 'play_civ.py --minutes 10', ('nightly',), minutes=13, fixture='civ_npc')
add('space_play', P + 'coop_space_play.py', ('smoke',), minutes=6)
add('space_page', P + 'coop_space_page.py', ('smoke',), minutes=4)  # the F9 Space page (space_ext.h), jump by a real click
add('space_map', P + 'coop_space_map.py', minutes=6)  # partners and their colonies on the galaxy map, the roster (space_map.h)
add('space_jump', P + 'coop_space_jump.py', minutes=7)  # jumps into unvisited systems: planets, first contact
add('space_share_takeoff', P + 'coop_space_share_takeoff.py', minutes=5)  # a guest's share saved as the host leaves a planet: the guest stays in the system (BUG-0301; SPORE_FIX_OFF=world.share_settle_context fails)
add('space_share_wait', P + 'coop_space_share_wait.py', minutes=5)  # a guest waiting for the world while the host is on a planet is told why; the host's trace says share_wait=planet (BUG-0382; SPORE_FIX_OFF=world.share_wait_notice fails)
add('space_share_landing', P + 'coop_space_share_landing.py', minutes=5)  # the host lands right after a guest's share save is posted: the save is made again off the planet (BUG-0357; SPORE_FIX_OFF=world.share_planet_save fails)
add('space_death', P + 'coop_space_death.py', minutes=5)
add('space_guest_ui', P + 'coop_space_guest_ui.py', minutes=4)  # a guest joining during the menu intro: no movie left over, real clicks work
add('space_puppet', P + 'coop_space_puppet.py', minutes=5)  # the game frees partner puppets
add('space_rescue', P + 'coop_space_rescue.py', minutes=6)  # disabled by real (relayed) NPC hits
add('space_helper', P + 'coop_space_helper.py', warm=True, minutes=3, fixture='space')  # a disabled partner rescues nobody (BUG-0131; SPORE_SPACE_FIX=0 fails)
add('space_stale', P + 'coop_space_stale.py', warm=True, minutes=5, fixture='space')  # hits about freed mirrors / puppets, star ids past the table, the second repair's first step (BUG-0054, BUG-0092, BUG-0147)
add('space_fleet', P + 'coop_space_fleet.py', warm=True, minutes=4, fixture='space')  # a lost ship-state chunk changes no mirror, the guest keeps its posse, a destroyed ship goes with 2 chunks and lost chunks (BUG-0037, BUG-0084, BUG-0230)
add('space_map_input', P + 'coop_space_map_input.py', minutes=7, fixture='space')  # x87 stack after contacts, jump notice, galaxy teleport guards, tag clicks from the window (BUG-0126, BUG-0072, BUG-0063, BUG-0090, BUG-0145; --without-fix fails)
add('space_seed_thread', P + 'coop_space_seed_thread.py', warm=True, minutes=5, fixture='space')  # another thread does not switch the planet seeding off (BUG-0123; --without-fix fails)
add('space_seed_profile', P + 'coop_space_seed_profile.py', minutes=4, fixture='space')  # an unvisited NPC star's planets do not depend on its species profile being loaded (BUG-0185; --without-fix fails)
add('space_land_early', P + 'coop_space_land_early.py', minutes=3, fixture='space')  # the wheel lands on the home planet right after a load (BUG-0182)
add('space_defenders', P + 'coop_space_defenders.py', warm=True, minutes=3, fixture='space')  # the game's Defenders are not partner puppets (BUG-0111; --without-fix fails)
add('space_mirror_model', P + 'coop_space_mirror_model.py', warm=True, minutes=3, fixture='space')  # a host ship whose model the guest lacks does not fault the guest (BUG-0323; --without-fix fails)
add('space_armada_return', P + 'coop_space_armada_return.py', minutes=8, fixture='space')  # an armada wave resumes its raid after the host's return (BUG-0046; --without-fix fails); a resumed wave's ships are tough, a raid without ships is not a won wave (BUG-0227; SPORE_FIX_OFF=contracts.wave_resume fails)
add('space_contracts', P + 'coop_space_contracts.py', minutes=9)  # co-op contracts: armada, survey, timeout (F9 Space page clicks)
add('space_rivalry', P + 'coop_space_rivalry.py', minutes=8)  # rivals mode: own budgets, war and peace by real clicks, PvP
add('space_diplomacy', P + 'coop_space_diplomacy.py', minutes=5)  # shared relations with NPC empires
add('space_diplo_mirror', P + 'coop_space_diplo_mirror.py', warm=True, minutes=3, fixture='space')  # the allies mirror's record is never shared by pol (BUG-0071)
add('space_trade', P + 'coop_space_trade.py', minutes=4)  # tool gifts and spice sales by real clicks (space_trade.cpp)
add('space_trade_refund', P + 'coop_space_trade_refund.py', warm=True, minutes=3, fixture='space')  # gifts that come back outside Space are owed, not lost (BUG-0058)
add('space_down_away', P + 'coop_space_down_away.py', minutes=5, fixture='space')  # a disabled ship and trips out of Space: paused in the same world, no death after a load, no invincibility left (BUG-0229, BUG-0228; SPORE_FIX_OFF=space.down_world / space.down_clear fail)
add('space_gift_refund', P + 'coop_space_gift_refund.py', warm=True, minutes=3, fixture='space')  # spice of a gift that comes back outside Space is put back once in Space (BUG-0226; SPORE_FIX_OFF=space.gift_refund fails)
# a partner's colonies as an allied empire in the guest's galaxy, its raid fought there, a clean save
add('space_allies', P + 'coop_space_allies.py', minutes=10)
add('space_allies_reload', P + 'coop_space_allies_reload.py', minutes=5, fixture='space')  # after a galaxy reload the allies mirror gets a new cEmpire (BUG-0038)
add('space_allies_reload_addr', P + 'coop_space_allies_reload.py --same-address', minutes=5, fixture='space')  # the same, with the new player cEmpire at the old address: the Space world epoch sees the reload (BUG-0225; SPORE_FIX_OFF=allies.reload_id fails)
add('space_trade_reload', P + 'coop_space_trade_reload.py', minutes=6, fixture='space')  # a trade refund that comes back while another game is loaded waits for its own game (BUG-0225; SPORE_FIX_OFF=space.owed_id fails)
add('space_trade_reload_same', P + 'coop_space_trade_reload.py --same-game', minutes=5, fixture='space')  # the same game loaded again (saved by the quit after the gift): paid once (BUG-0225, BUG-0058)
add('dead_modal', P + 'coop_dead_modal.py', warm=True, minutes=3, fixture='space')  # a destroyed modal window in the window manager does not fault the next mouse message (BUG-0270; SPORE_FIX_OFF=game.dead_modal fails)
add('space_finale', P + 'coop_space_finale.py', minutes=12)  # the galaxy core together (space_finale.cpp): wait, joint entry, Staff of Life, rejoin
add('space_finale_wait', P + 'coop_space_finale_wait.py', minutes=6)  # the host that waited at the core counts its entry as together when it enters first (BUG-0320; --without-fix fails); in the core each game draws the partner's ship where it is (BUG-0378; SPORE_FIX_OFF=space.puppet_pin fails)
add('space_drawn', P + 'coop_space_drawn.py', minutes=4, fixture='space')  # where each game draws the partner's ship and the host's NPC ships against where they are, both flying with real keys in a star system (BUG-0378, BUG-0190)
add('space_drawn_planet', P + 'coop_space_drawn.py --planet', minutes=4, fixture='space')  # the same on the fixture's planet (BUG-0190: mirrors and puppets at their own zoom altitude)
add('space_stray_window', P + 'coop_space_stray_window.py', minutes=4, fixture='space')  # a window that pauses the game without a caption (My Collections, Sporepedia) is closed by the Space bots (BUG-0336; OLD_FINALE_TOOL=1 fails)
add('space_finale_alone', P + 'coop_space_finale.py --alone', minutes=10)  # "Enter now" alone: the partner gets Steve's gift by message
add('space_captain', P + 'coop_space_captain.py', minutes=4, reports=True)  # BUG-0303: a captain screen left open (rank badge) blocks the finale's wheel zoom-out; the tool closes it (OLD_FINALE_TOOL=1 fails); BUG-0304: the game's own fault when it opens
# joins and rejoins over a bad network (goodbye lost every second round), the setup + in-game update
add('rejoin_mobile', 'SPORE_NET=mobile ' + P + 'coop_rejoin_net.py', minutes=5)
# many rejoins / world loads in a row, fault log checked after each (docs/re/space.md 8.3): a world sent
# again while the guest loads, a load right after the galaxy menu came up in fresh games, rejoins at
# random moments
# the mod confirms the host's "game saved" box without answering other menus (pause, exit question)
add('save_box', P + 'coop_save_box.py', ('full',), minutes=4)
add('rejoin_loop', P + 'coop_rejoin_loop.py --rounds 15 --reshare 0:2.5', minutes=5)
add('menu_load', P + 'coop_rejoin_loop.py --rounds 5 --local-load 0:1.5 --cold', minutes=5)
add('rejoin_race_long', P + 'coop_rejoin_loop.py --rounds 40 --race 1:9', ('nightly',), minutes=8)
add('rejoin_awful', 'SPORE_NET=awful ' + P + 'coop_rejoin_net.py', ('nightly',), minutes=8)
add('update', P + 'update_check.py', exclusive=True, minutes=6, uses_free_lane=True)  # alone: it rebuilds build/ and out/dist (package.sh) that win_unit runs (BUG-0211); a lane outside the run (SPORE_FREE_LANE), one game
add('update_api403', P + 'update_check.py --api403', ('nightly',), exclusive=True, minutes=6, uses_free_lane=True)  # the same with the API path answering 403: the release page's redirect (BUG-0392; SPORE_FIX_OFF=update.web_fallback fails)
# chaos bot (random real input by both players, network churn); smoke: 2 minutes of tribe
add('chaos_tribe', P + 'coop_chaos.py --fixture tribe --minutes 2', ('smoke',), minutes=3)
for st, m in (('creature', 6), ('cell', 6), ('tribe', 8), ('civ', 8), ('space', 6)):
    add(f'chaos_{st}_long', P + f'coop_chaos.py --fixture {st} --minutes {m}', ('nightly',), minutes=m + 1)
# two bots PLAY the Cell stage together (real input: swim, eat, flee, revive), sync / food / regression checks
add('play_cell', P + 'play_cell.py --minutes 10', ('nightly',), warm=True, minutes=12, fixture='cell')
# played like people play (real input, measurements; docs/stability/creature.md)
add('play_creature', P + 'play_creature.py --minutes 10', ('nightly',), minutes=12)
# games played by bots through real input for many minutes (docs/stability/<stage>.md)
add('play_tribe', P + 'play_tribe.py --minutes 10', ('nightly',), warm=True, minutes=12, fixture='tribe')
# the same tests over a bad network between guest and host (tools/tests/netem.py)
for name, cmd in (('tribe_coop', 'coop_tribe.py'), ('cell_coop', 'coop_cell.py'), ('civ_coop', 'coop_civ.py'),
                  ('team', 'coop_team.py'), ('space_coop', 'coop_space.py')):
    add(f'{name}_mobile', 'SPORE_NET=mobile ' + P + cmd, ('nightly',), minutes=4)
# the Space stage played by two bots with real input (docs/stability/space.md)
add('play_space', P + 'play_space.py --minutes 12', ('nightly',), minutes=14)
add('chaos_tribe_awful', 'SPORE_NET=awful ' + P + 'coop_chaos.py --fixture tribe --minutes 5', ('nightly',),
    minutes=6)

# paths outside the lab's other suites (BUG-0152, BUG-0162): `lab.py run extended`. Three players in
# one lane (the extra guest is instance 20+N in the lane's bottle, coop_multi.extra_instances)
add('hotreload', P + 'hotreload_check.py', ('extended',), minutes=3)  # mod.reload coop twice, joins again
add('multi3', P + 'coop_multi.py --fixture creature --players 3 --minutes 3', ('extended', 'multi'), minutes=7)
add('npc_multi3', P + 'coop_npc_multi.py --players 3 --seconds 40', ('extended',), minutes=5)

# 3-4 players per stage in minutes (BUG-0152, tools/tests/party.py, docs/06-testing.md#testing-party): joins of two
# guests at once, each guest's game shows the others, shared totals, a vote with N, a leave and rejoin of the
# last guest, the host in the menu with several guests. `lab.py run multi`; also nightly
for st, n, extra in (('cell', 3, ''), ('creature', 4, ''), ('tribe', 4, ''), ('civ', 3, ''), ('space', 4, ''),
                     ('creature', 3, ' --one-by-one --skip away')):
    add(f'party_{st}{n}' + ('_seq' if extra else ''), P + f'coop_party.py --fixture {st} --players {n}{extra}',
        ('multi', 'extended'), minutes=3)

# the gameplay paths between guests per stage (BUG-0152): each a few minutes on party.py
for name, cmd in (('party_cell_ko', 'coop_party_cell.py'), ('party_creature_npc', 'coop_party_creature.py'),
                  ('party_tribe_split', 'coop_party_tribe.py'), ('party_civ_own', 'coop_party_civ.py'),
                  ('party_space_names', 'coop_party_space.py'), ('party_civ_trade', 'coop_party_civ_trade.py')):
    add(name, P + cmd, ('multi', 'extended'), minutes=4)
# BUG-0295 / BUG-0298: four players in Cell far apart and close together: every game's cell pool stays under what
# the game's broadphase holds, each guest has the host's cells around it; SPORE_FIX_OFF=cell.budget fails
add('cell_4p', P + 'coop_cell_4p.py', ('multi', 'extended'), minutes=6)
add('cell_nan', P + 'coop_cell_nan.py', minutes=3)  # a guest's own cell NaN is put back, no tiles from NaN boxes, a level-up after it (BUG-0299; SPORE_FIX_OFF=cell.nan_guard fails)
add('cell_stale', P + 'coop_cell_stale.py', minutes=2)  # a group member drops a dead chase target copied from its leader: no fault per frame on the host (BUG-0306; SPORE_FIX_OFF=cell.pick_guard fails)
add('cell_nan_all', P + 'coop_cell_nan.py --all', minutes=3)  # the own cell NaN in every field: healed once, not NaN again (BUG-0307; SPORE_FIX_OFF=cell.nan_heal_all fails)
add('cell_lowfps', P + 'coop_cell_lowfps.py', minutes=5)  # a guest at 15 fps shows the host's moving cells where they are, not ~2 m behind (BUG-0282 guard: not reproduced on 5d7f20e)
add('cell_nan_drain', P + 'coop_cell_nan.py --all --drain 180', minutes=7)  # the own cell NaN in every field, level-ups, 3 min of play: the guest keeps its address space (BUG-0307)
add('cell_jumps', P + 'coop_cell_jumps.py', minutes=5)  # a guest's cell jumps 150-250 m 40 times: no pile of background cells, it mirrors the host again (BUG-0314; SPORE_FIX_OFF=cell.bg_cull fails)
add('cell_jumps_host', P + 'coop_cell_jumps.py --who host', minutes=5)  # the same on the host (BUG-0314)
add('cell_rejoin_leave', P + 'coop_rejoin_loop.py --fixture cell --rounds 12 --gap 0.3:1.5 --cell-leave', minutes=12)  # a guest rejoins in Cell, the host's world arrives and the stage is left: none of our cell work runs on the pool being torn down (BUG-0308 guard; SPORE_FIX_OFF=cell.leave_guard fails)
add('cell_churn', P + 'coop_cell_churn.py', minutes=4)  # the guest's mirrors removed and re-created 40 times: its free address space stays (BUG-0307)
add('cell_sap', P + 'coop_cell_sap.py', ('multi', 'extended'), minutes=6)  # four players, over 1024 host cells in one column: the broadphase sweep holds them, no fault per frame (BUG-0337; SPORE_FIX_OFF=cell.sap_sweep fails)
add('cell_pause', P + 'coop_cell_pause.py', minutes=4)  # a guest in its pause menu while its mirrors churn: the cell broadphase's pending list stays in bounds, its order intact, leaving Cell does not fault (BUG-0348; SPORE_FIX_OFF=cell.sap_pending fails)
add('cell_sync_check', P + 'coop_cell_sync_check.py', minutes=4)  # the sync-check finds no difference while food comes in and in a crowded pool with the guest swimming (BUG-0379; SPORE_FIX_OFF=diag.total_lag fails phase A)
add('cell_ko_food', P + 'coop_cell_ko_food.py', minutes=4)  # a knocked-out cell (guest, then host) eats nothing it touches: no food on either game (BUG-0364; SPORE_FIX_OFF=cell.ko_no_eat fails)
add('cell_particles', P + 'coop_cell_particles.py', minutes=3)  # a full soft-body particle pool on a guest while its mirrors churn: no fault, the pool's count sane, its cell in place (BUG-0338; SPORE_FIX_OFF=cell.particles fails)
add('cell_particles_nofill', P + 'coop_cell_particles.py --no-fill', ('extended',), minutes=3)  # control for cell_particles: the same mirror churn without a full pool (BUG-0355 follow-up)
add('cell_spare', P + 'coop_cell_spare.py', minutes=6)  # a crowd of cells on both games' screens: no soft-body particle lost, none shares the spare slot, then the end of Cell at over 10 fps (BUG-0355; SPORE_FIX_OFF=cell.gfx_full fails)

# a whole game of four bot players, Cell to the galactic core (BUG-0152, docs/06-testing.md#testing-campaign):
# `lab.py run campaign --lanes 1 --skip <every other lane>` (one lane; `run` has no --lane, BUG-0259). Four games in the lane's bottle (instances 20+N / 30+N), nothing else of
# the lab's suites; campaign_short is the first stage change only (a quick check of the tool itself)
add('campaign', P + 'coop_campaign.py --minutes 8', ('campaign',), minutes=150,
    timeout=5 * 3600)
add('campaign_short', P + 'coop_campaign.py --to creature --minutes 3 --advance-minutes 8', ('campaign',),
    minutes=25, timeout=3600)
# BUG-0276: the guests get the host's world after a live stage change with four players (the share
# save posted in the first frames of Tribe / of the solar system); a stuck finding is the bug.
# A timing race: FAIL with SPORE_FIX_OFF=world.share_settle_reset in about 1 of 3 games (--repeat)
add('campaign_tribe', P + 'coop_campaign.py --from creature --to tribe --minutes 2 --no-finale', ('campaign',),
    minutes=30, timeout=3600)
add('campaign_space', P + 'coop_campaign.py --from civ --to space --minutes 2 --no-finale', ('campaign',),
    minutes=35, timeout=3600)
# BUG-0359: the host's heap corruption that surfaced at Creature -> Tribe in a whole campaign (Cell ran
# in the same process first): Cell -> Creature -> Tribe with four players, the stress of that path
add('campaign_ct', P + 'coop_campaign.py --from cell --to tribe --minutes 2 --no-finale', ('campaign',),
    minutes=45, timeout=3600)


SUITES = ('smoke', 'full', 'nightly', 'extended', 'campaign', 'multi', 'players', 'mixed')


def select(spec):
    """suites (smoke / full / nightly / extended / multi / campaign / players) and test names, comma-separated and mixed: 'smoke,vote'"""
    names = {t['name'] for t in T}
    want = set()
    for w in (x.strip() for x in spec.split(',') if x.strip()):
        if w in SUITES:
            want |= {t['name'] for t in T if w in t['suites']}
        else:
            assert w in names, f'unknown test: {w} (known: {sorted(names)})'
            want.add(w)
    return [t for t in T if t['name'] in want]
add('trace_log', P + 'coop_trace_log.py', minutes=6)  # support logging: trace / eat / body lines on both games in Cell and Creature, the same eaten object in both logs, log size per minute


# session paths under the conditions of real players (feat/w33-tests-session, docs/06-testing.md#testing-session-paths)
add('tod_early', P + 'coop_tod_early.py', minutes=2)  # BUG-0433: a session hosted before the main menu (game mode 0) does not crash in the time-of-day send (SPORE_FIX_OFF=core.tod_guard fails)
add('latency_step_ingame', 'SPORE_NET="delay=30,jitter=5" ' + P + 'coop_latency_step.py', minutes=5)  # one-way delay 30 -> 300 -> 30 ms while the partner walks: the drawn puppet moves in steps / jumps for at most 5 s in a row (BUG-0133 in the game; SPORE_FIX_OFF=net.offset_window fails)
add('transition_net', 'SPORE_NET=player SPORE_FPS=guest:30 ' + P + 'coop_transition_net.py',
    ('nightly',), minutes=20, timeout=45 * 60,
    allow=[allow('sync-check', 'BUG-0413', 'the guest loses the host\'s NPCs under the brain-upgrade caption of the '
                 'Creature shortcut (AddEvolutionPoints 3000)', stage='Creature', kind='n missing')])  # Cell -> Creature -> Tribe the players' way (History, vote, editors, done) over a home connection, the guest at 30 fps: it follows, draws the host, no sync-check MISMATCH after the settle time (TRANSITION_PLANT=mismatch fails)
add('party_rejoin_net', 'SPORE_NET=players ' + P + 'coop_party_rejoin_net.py --players 4', ('multi', 'extended'), minutes=10)  # 4 players rejoin over their own bad networks (one, id swap, lost goodbye, two at once): together, drawn where they are, as their own species (PARTY_PLANT=lift fails; BUG-0076's switches did not)
add('join_during_combat', P + 'coop_join_during.py --case combat', minutes=4)  # a guest joins while the host fights an NPC: its game agrees with the host's within 10 s (host drawn, NPCs alive / dead / health)
add('join_during_host_editor', P + 'coop_join_during.py --case editor --allow-empty BUG-0412', minutes=5)  # a guest joins while the host is in its editor: agrees within 10 s of the host's return (host drawn as its species, NPCs)
add('vote_net_4p', 'SPORE_NET=players ' + P + 'coop_vote_net_4p.py', ('multi', 'extended'), minutes=10)  # votes of 4 players over bad networks: evolution vote, an action vote while a guest edits and leaves, its return, all four
# the previous release and this build in one session: suite `mixed`, only when asked for. The test also takes the
# neighbour lane (SPORE_LANE_B, default lane+1, lane 10: 9), which must be idle: `lab.py run mixed --lane 9` alone
# (lane 10 free). Not `exclusive` (that needs lanes 1-5, for the LAN tests)
add('mixed_version_guest', P + 'coop_mixed_version.py --old guest', ('mixed',), minutes=7)  # v0.7.3.3 joins this build's host: version lines, puppets, DNA cause tail and nest eggs to the old game (--old-tag v0.7.2.4 fails)
add('mixed_version_host', P + 'coop_mixed_version.py --old host', ('mixed',), minutes=7)  # this build joins a v0.7.3.3 host
add('mixed_version_tribe', P + 'coop_mixed_version.py --old guest --fixture tribe', ('mixed',), minutes=7)  # v0.7.3.3 joins this build's Tribe host (chunks with the time trailer)

# ---------------- generated variants: the conditions of real players (docs/06-testing.md#testing-degraded)
# `lab.py run players`: each chosen test again with the guest at 15 fps (frame.limit, twoinst.apply_fps), over
# a mobile network, over burst loss (netem.py Gilbert-Elliott), both, and four players. A variant is cold (other
# frame caps / proxies than the lane's warm games) and keeps its base's gates / allow / monitor.
VARIANTS = {
    'fps15': 'SPORE_FPS=guest:15 ',
    'mobile': 'SPORE_NET=mobile ',
    'burst': 'SPORE_NET=burst ',
    'fps15burst': 'SPORE_FPS=guest:15 SPORE_NET=burst ',
    'players': 'SPORE_NET=players ',
}
VARIANT_TESTS = ('creature', 'team', 'tribe_coop', 'cell_coop', 'civ_coop', 'space_coop', 'puppet_walk', 'npc_run_hp',
                 'sync', 'select', 'party_creature4', 'party_tribe4', 'party_space4')
FOUR_P = {'party_cell3': 'party_cell4', 'party_civ3': 'party_civ4', 'multi3': 'multi4', 'npc_multi3': 'npc_multi4'}


def _variants():
    by = {t['name']: t for t in T}
    out = []
    for base in VARIANT_TESTS:
        t = by[base]
        for v, env in VARIANTS.items():
            name = f'{base}_{v}'
            if name in by:  # an older hand-made variant (tribe_coop_mobile, ...): it joins the suite
                by[name]['suites'] = set(by[name]['suites']) | {'players'}
                continue
            out.append(dict(t, name=name, cmd=env + t['cmd'], suites={'players'}, warm=False,
                            minutes=t['minutes'] + 2, fixture=None, variant_of=base))
    for base, name in FOUR_P.items():
        t = by[base]
        out.append(dict(t, name=name, cmd=t['cmd'].replace('--players 3', '--players 4'), suites={'players'},
                        warm=False, minutes=t['minutes'] + 2, fixture=None, variant_of=base))
        out.append(dict(t, name=name + '_players', cmd='SPORE_NET=players ' + out[-1]['cmd'], suites={'players'},
                        warm=False, minutes=t['minutes'] + 3, fixture=None, variant_of=base))
    return out


T += _variants()
