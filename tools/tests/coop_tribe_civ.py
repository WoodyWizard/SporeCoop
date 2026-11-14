#!/usr/bin/env python3
"""The live Tribe -> Civ change with two players, the players' way (BUG-0576).

Players (0.7.5): "black screen after evolving into the city stage from Tribe": the host's vote ended, then "the
editor waits: the game is busy (paused)" and nothing for 50 s. At the end of Tribe the game itself pauses its time
and raises the input lock, then asks for the City Hall editor (docs/re/tribe.md#tribe-civ-transition); the vote's
busy gate waited for that pause, which only the editor lifts.

tribe fixture. The host's goal bar is filled (`coop.tribe advance`: cPlayer +0x10F0 = +0x10F4 and the stage's own
goal message, which shows the HUD's advance button); then as a player: the advance button, the stage card's
check, the caption of TRG2CVG_PreEditor, the guest's "I'm ready". Checked:
  * the host's game is in its editor within 15 s after the vote ended (the game's mode, not the mod's counters);
  * the host leaves the City Hall editor (done) and its game reaches the Civ stage;
  * the guest's game reaches the Civ stage too (it follows the host's world) within 5 min;
  * no fault on either game.

  * BUG-0585: in the vehicle editor (the City Hall stands, the tribe has no food mat) the guest changes its food
    (`coop.res add food 2`, as its tribe eats and gathers): the host's game takes it without a fault.

  --ground: then the ground round every city on both games (the BUG-0332 class in Civ, coop_civ_city_ground.py).

  --rejoin (BUG-0849): back from the City Hall editor (the end of Tribe: the game takes the village down for Civ) the
    guest leaves and joins again. The host's game must write nothing into its slot from the City Hall editor's end
    until it is in the Civ stage, and no fault; the guest gets the world in Civ and both games reach Civ.
    SPORE_FIX_OFF=tribe.graduation_save fails: the join's share save faults in the game's serializer at 0xBCAB07
    (data 0x60C; the fork saw the same address) and writes part of the slot (lab 20261112-021437).

  --player-editor (BUG-1096): the editors are left the campaign driver's way (coop_campaign.editor_parts_due,
    drag_slow); with SPORE_FPS=host:4 the host's game at a campaign's loaded-machine fps. CAMPAIGN_OLD_EDITOR_DRAG=1
    (one drag only, before the fix) fails when that drag is lost: the host stays in the City Hall editor.

  SPORE_FIX_OFF=core.editor_own_pause: the old gate: FAIL (the editor opens 60 s or more after the vote).
  SPORE_FIX_OFF=tribe.food_fx_mat: FAIL (the host faults at cTribe::UpdateFoodVisuals+0x28 and freezes).
"""
import os
import re
import sys
import time

import lane
import uifind
import worlddiff

from coop_ui import Ui
from twoinst import Checks, lane_state, shot, start

ADVANCE = 0x6454B47  # the HUD's advance button (shown when the goal bar is full)
CARD_OK = (1415, 1055)  # the stage card's check
CAPTION_OK = (1410, 1048)  # a caption's check
DONE = (1416, 1058)  # an editor's green check
DICE = (614, 826)  # the name panel's dice (a random name)
# three parts from the palette onto the plot: the building / vehicle editors save only with 3 parts and a name
PARTS = (((88, 130), (900, 850)), ((220, 130), (900, 700)), ((350, 130), (1150, 850)))


def mode(con):
    return con.call('game.mode')['out']


def status(con):
    return con.call('coop.status')['out']


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def where(con, wid):
    m = re.search(r'%08X (-?\d+) (-?\d+) (-?\d+) (-?\d+)' % wid, con.call('coop.ui where %08X' % wid)['out'])
    return tuple(int(v) for v in m.groups()) if m else None


def drag(con, a, b):
    con.call('move %d %d' % a)
    time.sleep(0.3)
    con.call('button %d %d down' % a)
    time.sleep(0.3)
    for f in (0.25, 0.5, 0.75, 1.0):
        con.call('move %d %d' % (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f))
        time.sleep(0.2)
    time.sleep(0.4)
    con.call('button %d %d up' % b)
    time.sleep(1)


def finish_editor(con):
    """the City Hall / vehicle editor the players' way: 3 parts, done (the name panel opens), its dice, done"""
    time.sleep(6)
    for a, b in PARTS:
        drag(con, a, b)
    con.call('click %d %d' % DONE)
    time.sleep(1.5)
    con.call('click %d %d' % DICE)
    time.sleep(1)
    con.call('click %d %d' % DONE)
    time.sleep(1.5)
    con.call('click %d %d' % DONE)


def leave_editor_player(con, tries=20):
    """--player-editor (BUG-1096): leave the editor the campaign driver's way (coop_campaign: "done" every few
    seconds, the parts dragged at the third try and, after the fix, again every 4 tries slower while the editor
    stays up), counting the tries; the City Hall's "You need at least 3 parts to Save" shown is printed"""
    import coop_campaign as cc
    n = 0
    while n < tries and 'Editor' in mode(con):
        time.sleep(4)
        pace = cc.editor_parts_due(n)
        if pace:
            for (sx, sy), (tx, ty) in PARTS:
                cc.drag_slow(con, sx, sy, tx, ty, pace)
            print(f'  try {n}: parts dragged (pace {pace:g}); "need 3 parts" shown: '
                  f'{bool(uifind.shown(con, "You need at least 3 parts to Save"))}', flush=True)
        if n >= 3:
            con.call('click %d %d' % DICE)
            time.sleep(1)
        con.call('click %d %d' % DONE)
        n += 1
        wait(lambda: 'Editor' not in mode(con), 4, 0.5)
    print(f'  editor left: {"Editor" not in mode(con)} after {n} "done" tries', flush=True)
    return n


def vote_active(con):
    return 'vote active' in con.call('coop.vote')['out']


def busy(con):
    m = re.search(r'^busy: (\S+)', status(con), re.M)
    return m.group(1) if m else '?'


def mismatches(con):
    m = re.search(r'mismatch (\d+)', con.call('coop.diag sync')['out'])
    return int(m.group(1)) if m else -1


def end_of_tribe(host, guest, ck):
    """BUG-0588 / BUG-0594: back from the City Hall editor the host's game takes its Tribe world down for Civ (every
    animal at (0 0 0), the village's totem pole gone); the guest's game keeps its world until the Civ world comes. The
    host's game says it is in the end of the stage (the player tribe without its food mat, coop.wstate `st phase stage_end=1`) and the
    sync-check compares nothing then (SPORE_FIX_OFF=diag.stage_end: a MISMATCH "n: apart")."""
    time.sleep(4)
    hs = worlddiff.state_dump(host)
    print(f'  end of Tribe: host stage_end={hs and hs.stage_end}, totem poles host '
          f'{sum(1 for x in (hs.planet if hs else []) if x["noun"] == 0x055CF865)}', flush=True)
    ck(hs and hs.stage_end, 'the host\'s game is in the end of Tribe after the City Hall editor (stage_end)')
    m0 = mismatches(host)
    host.call('coop.diag sync 600')  # only the checks asked here
    for _ in range(6):  # the animals go to (0 0 0) during the City Hall cinematic (18 s after, lab 20261104-224338)
        host.call('coop.diag sync now')
        time.sleep(5)
    m1 = mismatches(host)
    print(f'  host sync-check mismatches {m0} -> {m1}: ' + host.call('coop.diag sync')['out'].strip().splitlines()[-1][:200],
          flush=True)
    ck(m1 == m0, f'no sync-check MISMATCH while the host\'s game ends the Tribe stage ({m0} -> {m1})')
    host.call('coop.diag sync on')


def host_slot():
    st = lane_state()
    slot = (st.get('planet') or 'Game0/Zorispe').split('/')[0]
    return os.path.join(lane.HOST_DIR, 'Games', slot)


def newest_write(d):
    """the newest write of a file in the host's slot (a save writes several files; one that faults half way
    leaves some of them, lab 20261112-021437)"""
    return max((os.path.getmtime(os.path.join(d, f)) for f in os.listdir(d)), default=0)


def rejoin_in_end(host, guest, ck):
    """BUG-0849: a guest that joins in the end of Tribe"""
    print('  guest: ' + guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(5)
    print('  guest: ' + guest.call(lane.JOIN)['out'].strip(), flush=True)
    ok = wait(lambda: 'Bob' in status(host), 30, 1)
    ck(ok, 'the guest joined again in the end of Tribe')


def main():
    ck = Checks()
    host, guest = start(fixture='tribe')
    rejoin = '--rejoin' in sys.argv
    player_editor = '--player-editor' in sys.argv
    pf = host_slot()
    t_cityhall = None
    ck('Tribe' in mode(host) and 'Tribe' in mode(guest), 'both games in Tribe')
    print(' ', host.call('coop.tribe advance')['out'].strip(), flush=True)
    r = wait(lambda: where(host, ADVANCE), 15)
    if not ck(r, 'the HUD shows the advance button (the goal bar is full)'):
        return ck.result()
    host.call('click %d %d' % ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2))
    time.sleep(5)
    shot(host, 'tribe_civ_card')
    # the stage card, then TRG2CVG_PreEditor: its caption waits for the check (the game pauses under it)
    t0 = time.time()
    while time.time() - t0 < 120 and not vote_active(host) and 'Editor' not in mode(host):
        b = busy(host)
        if 'cinematic' not in b:
            host.call('click %d %d' % CARD_OK)
        elif 'paused' in b:
            time.sleep(2)
            host.call('click %d %d' % CAPTION_OK)
        time.sleep(3)
    shot(host, 'tribe_civ_vote')
    if not ck(vote_active(host), "the game's City Hall editor request starts the vote"):
        print(host.call('coop.vote')['out'])
        return ck.result()
    gu = Ui(guest, 'guest')
    gu.wait(lambda s: s['ready']['vis'], 15, 'ready button')
    gu.click('ready')
    ck(wait(lambda: not vote_active(host), 20, 0.5), 'the vote ends when the guest is ready')
    t_end = time.time()
    print('  host busy at the vote end:', busy(host), flush=True)
    ed = wait(lambda: 'Editor' in mode(host), 90, 0.5)
    late = time.time() - t_end
    print('  ', host.call('coop.vote')['out'].strip().splitlines()[-1], flush=True)
    ck(ed and late < 15, f'the host is in its editor {late:.1f} s after the vote (at most 15 s)')
    if not ed:
        shot(host, 'tribe_civ_stuck')
        ck.faults(host=host, guest=guest)
        return ck.result()
    # the City Hall editor, then (TRG2CVG_ShowCityHall) a second vote and the vehicle editor, then the Civ stage
    t0 = time.time()
    editors = 0
    checked_end = False
    end_writes = []
    while time.time() - t0 < (900 if player_editor else 300) and 'Civ' not in mode(host):
        m = mode(host)
        if t_cityhall and editors < 2 and 'Editor' not in m and newest_write(pf) > t_cityhall:
            end_writes.append(newest_write(pf) - t_cityhall)
        if 'Editor' in m:
            editors += 1
            print(f'  host editor {editors}: {host.call("coop.vote")["out"].strip().splitlines()[-1]}', flush=True)
            if editors >= 2:
                # BUG-0585: the City Hall stands, the tribe's food mat is gone; the guest's tribe still eats and
                # gathers: its food changes reach the host (the shared food) while the host is in this editor
                for _ in range(3):
                    print('  guest food:', guest.call('coop.res add food 2')['out'].strip(), flush=True)
                    time.sleep(2)
                ck(host.call('game.mode') and 'total faults: 0' in host.call('exc')['out'],
                   "the host's game takes the guest's food changes in the vehicle editor without a fault")
            if player_editor:
                left = leave_editor_player(host)
                ck('Editor' not in mode(host), f'the host leaves editor {editors} the players\' way ({left} "done" tries, '
                   'BUG-1096)')
                if 'Editor' in mode(host):
                    shot(host, 'tribe_civ_editor_stuck')
                    return ck.result()
            else:
                finish_editor(host)
            wait(lambda: 'Editor' not in mode(host), 20)
            if editors == 1 and not checked_end and 'Tribe' in mode(host):
                checked_end = True
                time.sleep(3)  # (the game writes into the slot itself when the City Hall editor ends)
                t_cityhall = max(time.time(), newest_write(pf) + 0.5)
                print(f'  back from the City Hall editor; the host\'s slot last written {t_cityhall - newest_write(pf):.0f} s '
                      'ago', flush=True)
                if rejoin:
                    rejoin_in_end(host, guest, ck)
                end_of_tribe(host, guest, ck)
            continue
        b = busy(host)
        if 'paused' in b and 'cinematic' in b:
            host.call('click %d %d' % CAPTION_OK)
        if vote_active(host):
            gu.wait(lambda s: s['ready']['vis'], 15, 'ready button')
            gu.click('ready')
            t_end = time.time()
            ed = wait(lambda: 'Editor' in mode(host), 90, 0.5)
            late = time.time() - t_end
            ck(ed and late < 15, f'the host is in its next editor {late:.1f} s after the vote (at most 15 s)')
            continue
        time.sleep(3)
    shot(host, 'tribe_civ_host')
    t_civ = time.time()
    ck('Civ' in mode(host), 'the host reaches the Civ stage (%s)' % mode(host).strip())
    if rejoin and t_cityhall:
        # the slot's files, sampled from the City Hall editor's end until the vehicle editor opens: a save then (the
        # guest's join) is a write after the City Hall editor's end. (The game itself writes into the slot when the
        # vehicle editor ends, lab 20261112-022253: not sampled.)
        print(f'  writes into the host\'s slot seen in the end of Tribe: {sorted(set(round(w) for w in end_writes))} s '
              f'after the City Hall editor; Civ at {t_civ - t_cityhall:+.0f} s', flush=True)
        ck(not end_writes, 'no save into the host\'s slot in the end of Tribe (the joined guest gets the world in Civ)')
        ck('total faults: 0' in host.call('exc')['out'].splitlines()[0],
           'no fault in the host\'s game in the end of Tribe (the fork: the serializer at 0xBCAB07 during that save)')
    g = wait(lambda: 'Civ' in mode(guest), 300, 3)
    shot(guest, 'tribe_civ_guest')
    ck(g, "the guest's game follows into the Civ stage (%s)" % mode(guest).strip())
    if g and '--ground' in sys.argv:
        # the BUG-0332 class in Civ: the host made its first city live (its stamps), the guest loaded it; the ground
        # round every city on both games (coop_civ_city_ground.py's grid)
        from coop_civ_city_ground import survey
        time.sleep(20)
        bad, worst, n = survey(host, guest, 'after the live Tribe -> Civ change')
        ck(n > 0 and not bad, f'the ground round the cities is the same on both games ({len(bad)} of {n} points off, '
                              f'largest {worst:.2f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (TimeoutError, OSError) as e:  # a game froze (BUG-0585): its console stops answering
        print(f'  FAIL a game stopped answering its console ({e!r})', flush=True)
        print('RESULT: FAIL', flush=True)
        sys.exit(1)
