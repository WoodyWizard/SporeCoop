"""Every automated test with its properties (used by tools/lab.py and tools/tests/run_all.sh).

  name      result logs: out/lab/<run>/<name>.log
  cmd       command line, run from the repository root with SPORE_LANE set
  suites    smoke (before a merge, ~10 min on 3 lanes), full (every test), nightly (full + long),
            extended (paths outside the other suites: 3-4 players, hot reload; also nightly),
            campaign (a whole game of four bot players, coop_campaign.py: in no other suite, run it alone)
  warm      may reuse running games of the lane (twoinst.start with SPORE_WARM=1: the fixture is
            loaded into a fresh save slot, the guest rejoins; no restart). Tests that change the
            profile, enter editors or start their own games are cold.
  exclusive needs the machine alone (LAN discovery in the UI test sees other lanes' hosts)
  minutes   rough length, the scheduler starts long tests first
  timeout   seconds before the lab kills the test (default lab.TIMEOUT, 30 min)
  uses_free_lane  the test plays on SPORE_FREE_LANE (outside the run), takes that lane's lock
            (<lab folder>/.update_check-lane<N>.lock) and prints `LAB-WATCH lane=<N> since=<unix time>`
            once it holds it: the lab watches that lane for crash / hang reports and dead games from
            then on, and cleans it after an interrupt only when the lock is free (BUG-0249, BUG-0250)
"""

T = []


def add(name, cmd, suites=('full',), warm=False, exclusive=False, minutes=4, fixture=None, reports=False,
        uses_free_lane=False, timeout=None):
    """reports=True: the test makes crash / hang reports or faults on purpose and checks them itself;
    for every other test the lab fails it on a new crash-*/report-* file of its lane, a game that
    dies right after it, or a non-zero fault count in its log (tools/lab.py run_one, BUG-0036/0042)"""
    if 'campaign' in suites:  # hours of play on one lane: only when asked for
        suites = set(suites)
    else:
        suites = set(suites) | ({'nightly'} if {'nightly', 'extended'} & set(suites) else {'full', 'nightly'})
    T.append(dict(name=name, cmd=cmd, suites=suites, warm=warm,
                  exclusive=exclusive, minutes=minutes, fixture=fixture, reports=reports,
                  uses_free_lane=uses_free_lane, timeout=timeout))


P = 'python3 tools/tests/'
add('unit', 'make test', ('smoke',), minutes=1)
add('win_unit', P + 'win_unit.py', minutes=1)  # Win32 file handling under Wine: updater backups / pending list / duplicate files, received world slots (BUG-0035/0087/0089/0105/0114/0150; --old fails)
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
add('cell_spawn_guard', P + 'coop_cell_spawn_guard.py', warm=True, minutes=3, fixture='cell')  # coop.cell spawn with a slot that is not a live cell: "no such cell", no fault (BUG-0093)
add('dna_resend', P + 'coop_dna_resend.py', warm=True, minutes=3, fixture='creature')  # the DNA total reaches a guest that missed it (BUG-0128)
add('leave_cleanup', P + 'coop_leave_cleanup.py', minutes=4, fixture='space')  # every session end cleans every stage module (BUG-0033): Space mirrors after Leave, host while connected
add('space_coop', P + 'coop_space.py', ('smoke',), warm=True, minutes=2, fixture='space')
add('stage_switch', P + 'coop_stage_switch.py', minutes=13)  # every stage pair that matters, in game
add('vote', P + 'coop_vote.py', minutes=5)
add('vote_stale', P + 'coop_vote_stale.py', minutes=5)  # a vote ends with its session and its stage (BUG-0043)
add('world_queue', P + 'coop_world_queue.py', minutes=6)  # one save for shares waiting at once, shares of gone players dropped, no load inside an editor, no host world after Leave (BUG-0112/0166/0157/0074; --without all fails)
add('join_other_world', P + 'coop_join_other_world.py', minutes=5)  # a guest in another world of the stage gets no DNA / NPC removal from the host (BUG-0130; --without-fix fails)
add('leave_during_share', P + 'coop_leave_during_share.py', minutes=4)  # Leave right after a share save: the host can still leave the stage (BUG-0140)
add('exit_thread', P + 'coop_exit_thread.py', minutes=2, reports=True)  # ExitProcess without Shutdown: no abort from the keepalive std::thread (BUG-0107)
add('fix_registry', P + 'coop_fix_registry.py', minutes=2)  # coop.fix lists every fix switch, all off / on round-trips, the old commands share the bools (BUG-0116)
add('ui_status', P + 'coop_ui_status.py', minutes=7)  # the co-op window: port check, a pasted address, the status after the join and the session end, LAN pages, no "host is back" after a disconnect (BUG-0138, BUG-0104, BUG-0183, BUG-0085; --without-fix fails)
add('join_name', P + 'coop_join_name.py', warm=True, minutes=4)  # join by host name with a slow name server: the frames go on, the guest gets in (BUG-0204; --without-fix fails)
add('ui_menu_leave', P + 'coop_ui_menu_leave.py', minutes=2)  # the galaxy menu's Co-op item is dropped as the game leaves the menu (BUG-0163)
add('reload_update', P + 'coop_reload_update.py', minutes=2)  # hot reload while the update check waits: the old thread stays in the old image (BUG-0060)
add('guest_editor', P + 'coop_guest_editor.py', minutes=4)
add('notify', P + 'coop_notify.py', minutes=3)
add('window_layout', P + 'coop_window_layout.py', minutes=3)  # the co-op window re-centred after a smaller resolution, the status block never over the window or the panels (BUG-0113/0155; --old fails)
add('sync', P + 'coop_sync.py', warm=True, minutes=6)
add('creations', P + 'coop_creations.py', minutes=4)
add('net_robust', P + 'coop_net_robust.py', minutes=4)  # malformed messages from a joined player (BUG-0201)
add('chat_long', P + 'coop_chat_long.py', warm=True, minutes=2, fixture='creature')  # a reliable message bigger than a packet is split, nothing behind it stalls (BUG-0034)
add('creations_cell', P + 'coop_creations_cell.py', minutes=4)
add('creations_rejoin', P + 'coop_creations_rejoin.py', minutes=5)  # a newcomer under a reused id is not shown with the previous player's creature (BUG-0076)
add('editor_desync', P + 'coop_editor_desync.py', minutes=7)  # mating call and editors in Cell and Creature, real input
add('creature_desync', P + 'coop_creature_desync.py', minutes=4)  # editors, a crash and a reload: companion of a host-only species, no NPC copies, kills, the guest's look
add('editor_strays', P + 'coop_editor_strays.py', minutes=6)  # two evolutions: no puppet left over, herd homes on the surface (crash 0xB88BEA)
add('creature_e84', P + 'coop_creature_e84.py', minutes=5)  # a save with the partner's puppet, loaded after a restart: no crash on attack (0xD48634), removed
add('save_hygiene', P + 'coop_save_hygiene.py', minutes=5)  # a save made with the partner's puppet holds no puppet, no herd of ours after a restart (BUG-0020; SPORE_SAVE_FIX=0 fails)
add('alt_enter', P + 'coop_alt_enter.py', minutes=4)  # Alt+Enter / Alt+Shift+Enter in co-op: no fault at 0x7777BE (device recreation), on the ground
add('nest_species', P + 'coop_nest_species.py', minutes=4)  # the guest owns another creation under the key of the host's: the host's species at its nests, its table
add('migration', P + 'coop_migration.py', minutes=5)  # brain upgrade, editors, migration; both claim the new nest: no fault on the guest (0xC6AE60)
add('select', P + 'coop_select.py', ('smoke',), warm=True, minutes=3, fixture='creature')
add('social_console', P + 'coop_social_console.py', warm=True, minutes=3, fixture='creature')  # digits typed in the open cheat console play no social ability (BUG-0149; --without-fix fails)
add('creature_kills', P + 'coop_creature_kills.py', minutes=3)  # a guest's kill dies on the guest too (no 0 hp mirror standing), NPCs notice the partner
add('creature_death', P + 'coop_creature_death.py', minutes=4)  # alone: die, skip the caption and the respawn cutscene
add('creature_respawn', P + 'coop_creature_respawn.py', minutes=5)  # both players die (also after a pack rescue): the hatchling grows up and walks
add('diag', P + 'diag_check.py --crash', minutes=5, reports=True)  # kept logs, environment, version in the menu, report button, hang / crash reports
add('kit_freeze', P + 'kit_freeze.py', minutes=4, reports=True)  # stack / hangdump under a held heap, cheat-console waits, faults inside the log / fault logger, hook churn, hooks made while threads run the function (BUG-0050/0065/0115/0132/0202; --old: every case fails)
add('kit_docs', P + 'kit_profile_docs.py', minutes=2)  # a test profile's My Spore Creations is in the profile, the bottle's own Documents unchanged (BUG-0156; --old fails)
add('kit_crash', P + 'kit_crash.py', minutes=4, reports=True)  # crash reports for a stack overflow and a failed archive (out of memory); old reports pruned (BUG-0056/0127/0137; --old: every case fails)
# frames from the game loop when Present is lost (issue #4, the Steam overlay on Windows)
add('frames_loop', P + 'coop_frames_loop.py', ('smoke',), warm=True, minutes=2, fixture='creature')
add('exit_menu', P + 'coop_exit_menu.py', minutes=3)  # a guest exits to the main menu and comes back
add('host_away_kill', P + 'coop_host_away_kill.py', minutes=4)  # BUG-0082: the guest's kill list does not outlive the host's absence
add('npc_reuse', P + 'coop_npc_reuse.py', minutes=3)  # BUG-0044 / BUG-0023: a reused NPC object gets a new id (the guest's mirror is alive), ids never repeat
add('cinematic_puppet', P + 'coop_cinematic_puppet.py', minutes=3)  # BUG-0066: a puppet removed during a cinematic stays a puppet (no host NPC id)
add('npc_swap', P + 'coop_npc_swap.py', minutes=3)  # BUG-0110: an NPC tick that does not return leaves no puppet as the game's avatar
add('world_reload', P + 'coop_world_reload.py', minutes=6)  # BUG-0119: after a reload the guest plays its own creature again although the avatar's address came back (--without-fix fails)
add('npc_rejoin_paused', P + 'coop_npc_rejoin_paused.py', minutes=4)  # BUG-0125: a guest that rejoins while the host is paused gets the NPCs around it (--without-fix fails)
add('evolve_world', P + 'coop_evolve_world.py', minutes=6)  # BUG-0019: the world after an evolution that starts a migration waits for its cinematic: the guest grows up and migrates (--without-fix fails)
add('creation_leave', P + 'coop_creation_leave.py', minutes=5)  # BUG-0055: after leaving, the guest's own creation under the host's key is its own again
add('anim_teardown', P + 'coop_anim_teardown.py', minutes=8)  # BUG-0091: no animation on a puppet freed by a world teardown
add('join_new_game', P + 'coop_join_new_game.py', minutes=5)  # the host starts a new game (typed planet name) next to an old one: the guest gets the new one
add('tribe_play', P + 'coop_tribe_play.py', ('smoke',), warm=True, minutes=3, fixture='tribe')
add('civ_play', P + 'coop_civ_play.py', warm=True, minutes=3, fixture='civ_npc')
add('civ_reload', P + 'coop_civ_reload.py', warm=True, minutes=5, fixture='civ_npc')  # tables after a load and a leave (BUG-0040, BUG-0059; --nofix fails)
add('civ_shared', P + 'coop_civ_shared.py', warm=True, minutes=4, fixture='civ')  # crossing spends, offline money, roles on a rejoin (BUG-0106, BUG-0049, BUG-0064; --nofix fails)
add('civ_city_join', P + 'coop_civ_city_join.py', warm=True, minutes=5, fixture='civ_npc')  # an NPC city founded while the guest joins (BUG-0120; --nofix fails)
add('civ_ai_spawn', P + 'coop_civ_ai_spawn.py', warm=True, minutes=4, fixture='civ_npc')  # the NPC AI spawns no vehicles on the guest (BUG-0188; --nofix fails)
add('civ_leave', P + 'coop_civ_leave.py', warm=True, minutes=4, fixture='civ')  # the vehicles of a guest who left go to a present player (BUG-0031; --nofix fails)
add('civ_sw_vote', P + 'coop_civ_sw_vote.py', warm=True, minutes=3, fixture='civ')  # a second superweapon request does not change the running vote (BUG-0117; --nofix fails)
add('civ_caption', P + 'coop_civ_caption.py', warm=True, minutes=3, fixture='civ_npc')  # the host's capture caption closes by itself with guests (BUG-0187; --nofix fails)
add('civ_away', P + 'coop_civ_away.py', warm=True, minutes=5, fixture='civ')  # the guest's vehicles drive while the host is in the menu (BUG-0151; --nofix fails)
add('tribe_away', P + 'coop_tribe_away.py', warm=True, minutes=5, fixture='tribe')  # the guest's members walk while the host is in the menu (BUG-0151; --nofix fails)
add('dna_away', P + 'coop_dna_away.py', warm=True, minutes=5, fixture='creature')  # guest DNA while the host has no creature reaches the pack (BUG-0151; --nofix fails)
add('away_editor', P + 'coop_away_editor.py', warm=True, minutes=6, fixture='creature')  # a guest's goal reward reaching the host in its editor is paid when it is back (BUG-0205; --nofix fails)
add('tribe_chief_removed', P + 'coop_tribe_chief_removed.py', warm=True, minutes=3, fixture='tribe')  # a chief removed at once (BUG-0081; --nofix fails)
add('tribe_epic', P + 'coop_tribe_epic.py', warm=True, minutes=4, fixture='tribe')  # epic hitters, herd, save, chiefs after the session (BUG-0146, BUG-0121, BUG-0067; --nofix fails)
add('tribe_gameover', P + 'coop_tribe_gameover.py', minutes=4)  # a member added during the game-over caption (BUG-0172; --nofix fails)
add('tribe_ko', P + 'coop_tribe_ko.py', warm=True, minutes=3, fixture='tribe')  # a knocked-out NPC member lies down on the guest (BUG-0173; --nofix fails)
# two bots play Civilization for 10 minutes through real input (docs/stability/civ.md)
add('play_civ', P + 'play_civ.py --minutes 10', ('nightly',), minutes=13, fixture='civ_npc')
add('space_play', P + 'coop_space_play.py', ('smoke',), minutes=6)
add('space_page', P + 'coop_space_page.py', ('smoke',), minutes=4)  # the F9 Space page (space_ext.h), jump by a real click
add('space_map', P + 'coop_space_map.py', minutes=6)  # partners and their colonies on the galaxy map, the roster (space_map.h)
add('space_jump', P + 'coop_space_jump.py', minutes=7)  # jumps into unvisited systems: planets, first contact
add('space_death', P + 'coop_space_death.py', minutes=5)
add('space_guest_ui', P + 'coop_space_guest_ui.py', minutes=4)  # a guest joining during the menu intro: no movie left over, real clicks work
add('space_puppet', P + 'coop_space_puppet.py', minutes=5)  # the game frees partner puppets
add('space_rescue', P + 'coop_space_rescue.py', minutes=6)  # disabled by real (relayed) NPC hits
add('space_helper', P + 'coop_space_helper.py', warm=True, minutes=3, fixture='space')  # a disabled partner rescues nobody (BUG-0131; SPORE_SPACE_FIX=0 fails)
add('space_stale', P + 'coop_space_stale.py', warm=True, minutes=5, fixture='space')  # hits about freed mirrors / puppets, star ids past the table, the second repair's first step (BUG-0054, BUG-0092, BUG-0147)
add('space_fleet', P + 'coop_space_fleet.py', warm=True, minutes=3, fixture='space')  # a lost ship-state chunk changes no mirror, the guest keeps its posse (BUG-0037, BUG-0084)
add('space_map_input', P + 'coop_space_map_input.py', minutes=7, fixture='space')  # x87 stack after contacts, jump notice, galaxy teleport guards, tag clicks from the window (BUG-0126, BUG-0072, BUG-0063, BUG-0090, BUG-0145; --without-fix fails)
add('space_seed_thread', P + 'coop_space_seed_thread.py', warm=True, minutes=5, fixture='space')  # another thread does not switch the planet seeding off (BUG-0123; --without-fix fails)
add('space_defenders', P + 'coop_space_defenders.py', warm=True, minutes=3, fixture='space')  # the game's Defenders are not partner puppets (BUG-0111; --without-fix fails)
add('space_armada_return', P + 'coop_space_armada_return.py', minutes=5, fixture='space')  # an armada wave resumes its raid after the host's return (BUG-0046; --without-fix fails)
add('space_contracts', P + 'coop_space_contracts.py', minutes=9)  # co-op contracts: armada, survey, timeout (F9 Space page clicks)
add('space_rivalry', P + 'coop_space_rivalry.py', minutes=8)  # rivals mode: own budgets, war and peace by real clicks, PvP
add('space_diplomacy', P + 'coop_space_diplomacy.py', minutes=5)  # shared relations with NPC empires
add('space_diplo_mirror', P + 'coop_space_diplo_mirror.py', warm=True, minutes=3, fixture='space')  # the allies mirror's record is never shared by pol (BUG-0071)
add('space_trade', P + 'coop_space_trade.py', minutes=4)  # tool gifts and spice sales by real clicks (space_trade.cpp)
add('space_trade_refund', P + 'coop_space_trade_refund.py', warm=True, minutes=3, fixture='space')  # gifts that come back outside Space are owed, not lost (BUG-0058)
# a partner's colonies as an allied empire in the guest's galaxy, its raid fought there, a clean save
add('space_allies', P + 'coop_space_allies.py', minutes=10)
add('space_allies_reload', P + 'coop_space_allies_reload.py', minutes=5, fixture='space')  # after a galaxy reload the allies mirror gets a new cEmpire (BUG-0038)
add('space_finale', P + 'coop_space_finale.py', minutes=12)  # the galaxy core together (space_finale.cpp): wait, joint entry, Staff of Life, rejoin
add('space_finale_alone', P + 'coop_space_finale.py --alone', minutes=10)  # "Enter now" alone: the partner gets Steve's gift by message
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
add('multi3', P + 'coop_multi.py --fixture creature --players 3 --minutes 3', ('extended',), minutes=7)
add('npc_multi3', P + 'coop_npc_multi.py --players 3 --seconds 40', ('extended',), minutes=5)

# a whole game of four bot players, Cell to the galactic core (BUG-0152, docs/06-testing.md#testing-campaign):
# `lab.py run campaign --lane N`. Four games in the lane's bottle (instances 20+N / 30+N), nothing else of
# the lab's suites; campaign_short is the first stage change only (a quick check of the tool itself)
add('campaign', P + 'coop_campaign.py --minutes 8 --advance-minutes 20', ('campaign',), minutes=150,
    timeout=5 * 3600)
add('campaign_short', P + 'coop_campaign.py --to creature --minutes 3 --advance-minutes 8', ('campaign',),
    minutes=25, timeout=3600)


def select(spec):
    """suites (smoke / full / nightly / extended / campaign) and test names, comma-separated and mixed: 'smoke,vote'"""
    names = {t['name'] for t in T}
    want = set()
    for w in (x.strip() for x in spec.split(',') if x.strip()):
        if w in ('smoke', 'full', 'nightly', 'extended', 'campaign'):
            want |= {t['name'] for t in T if w in t['suites']}
        else:
            assert w in names, f'unknown test: {w} (known: {sorted(names)})'
            want.add(w)
    return [t for t in T if t['name'] in want]
