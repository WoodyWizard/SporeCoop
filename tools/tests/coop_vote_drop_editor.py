#!/usr/bin/env python3
"""A guest's own evolution editor opens only for a vote that passed, in a session that still runs
(BUG-0234, GitHub #168). Oracle: the guest's game mode (`game.mode`), not a mod counter.

  1. a passed vote, then the guest leaves while its own editor is still due (held with the test hook
     `coop.vote hold <s>`, so the Leave lands before the editor would open): the guest's game must
     stay in the Creature stage after the hold (0.7.3.0 opened the editor offline)
  2. a cancelled vote: the host requests the editor through the vote, the guest is not ready, the
     host goes to the galaxy menu (the vote is cancelled, BUG-0043): the guest's game must stay in
     the Creature stage (0.7.3.0 opened the guest's editor on the next frame)

  coop_vote_drop_editor.py --case 1|2 [--nofix]   (--nofix: core.own_editor_end and
                                        core.vote_drop_editor off, the test must FAIL)
"""
import re
import sys
import time

from twoinst import Checks, start

DONE = (1416, 1062)  # the green "done" check of the creature editor (1440x1080 test window)


def mode(con):
    return con.call('game.mode')['out']


def vote(con):
    return con.call('coop.vote')['out']


def stays(con, m0, seconds):
    """the game's mode stays m0 for `seconds`; returns the first other mode seen, or None"""
    t0 = time.time()
    while time.time() - t0 < seconds:
        m = mode(con)
        if m != m0:
            return m
        time.sleep(0.5)
    return None


def back_to(con, m0, t=60):
    """leave an editor with "done" until the game is in m0 again"""
    t0 = time.time()
    while time.time() - t0 < t and mode(con) != m0:
        con.call('click %d %d' % DONE)
        time.sleep(3)
    return mode(con) == m0


def main():
    nofix = '--nofix' in sys.argv
    host, guest = start()
    ck = Checks()
    for c in (host, guest):
        for f in ('core.own_editor_end', 'core.vote_drop_editor'):
            print(' ', c.call(f'coop.fix {f} {"off" if nofix else "on"}')['out'].strip())
    time.sleep(2)
    m0 = mode(guest)
    print('  stage mode:', m0)
    # each case on fresh games: a rejoin between them loads a host save that holds the guest's
    # puppet (another bug, a caught fault in Herd_Update on the guest)
    if '--case' not in sys.argv or sys.argv[sys.argv.index('--case') + 1] == '1':
        passed_then_leave(host, guest, ck, m0)
    if '--case' not in sys.argv or sys.argv[sys.argv.index('--case') + 1] == '2':
        cancelled(host, guest, ck, m0)
    for c in (host, guest):
        for f in ('core.own_editor_end', 'core.vote_drop_editor'):
            c.call(f'coop.fix {f} on')
    ck.faults(host=host, guest=guest)
    return ck.result()


def passed_then_leave(host, guest, ck, m0):
    """1. a passed vote, the guest leaves with its own editor still due"""
    print(' ', guest.call('coop.vote hold 40')['out'].strip().splitlines()[0])
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    t0 = time.time()
    while time.time() - t0 < 15 and 'vote active' not in vote(guest):
        time.sleep(0.5)
    ck('vote active' in vote(guest), 'the guest sees the evolution vote')
    host.call('coop.vote go')
    due = False
    t0 = time.time()
    while time.time() - t0 < 15 and not due:
        due = 'own editor due 1' in vote(guest)
        time.sleep(0.3)
    ck(due, 'the passed vote made the guest\'s own editor due')
    print(' ', guest.call('coop.leave')['out'].strip())
    other = stays(guest, m0, 55)
    print(f'  guest mode after the Leave and the hold: {other or m0}')
    ck(other is None, f'no own editor opened after the session ended ({other})')
    if other:
        ck(back_to(guest, m0), 'the guest left the editor it should not have had')
    ck(back_to(host, m0, 90), 'the host is back in its stage after its editor')


def cancelled(host, guest, ck, m0):
    """2. a cancelled vote"""
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    t0 = time.time()
    while time.time() - t0 < 15 and 'vote active' not in vote(guest):
        time.sleep(0.5)
    ck('vote active' in vote(guest), 'the guest sees the second vote')
    print(' ', host.call('coop.menu')['out'].strip())
    t0 = time.time()
    other = None
    in_menu = False
    while time.time() - t0 < 90 and not in_menu and not other:
        in_menu = 'GalaxyGameEntry' in mode(host)
        m = mode(guest)
        other = m if m != m0 else None
        time.sleep(0.5)
    ck(in_menu or other, 'the host left the stage for the galaxy menu')
    if not other:
        other = stays(guest, m0, 20)
    print(f'  guest mode after the cancelled vote: {other or m0}; host: {vote(host).splitlines()[0]}')
    ck(other is None, f'the cancelled vote opened no editor on the guest ({other})')
    ck(re.search(r'votes dropped [1-9]', vote(host)) is not None, 'the host cancelled the vote')
    if other:
        back_to(guest, m0)


if __name__ == '__main__':
    raise SystemExit(main())
