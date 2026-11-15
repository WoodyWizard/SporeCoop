#!/usr/bin/env python3
"""A vote ends with its session and its stage (BUG-0043): its editor request or action is dropped,
not run later in another session or after the stage is gone (the civ superweapon used to fire on
the freed civilization of the planet the host had left).

  1. a real editor request through the vote, then the host's Leave and Host again: the host's game
     must stay in its stage (on the old code the stale vote ended "alone" and the editor opened);
     a new vote then starts and reaches the guest's window
  2. an action vote (coop.vote action, the superweapon's kind), then the host leaves the stage for
     the galaxy menu and the guest presses "I'm ready": the action must not run

  coop_vote_stale.py [--without-fix]   (--without-fix: coop.fix core.vote_scope off, the test must FAIL)"""
import re
import sys
import time

import lane
from coop_ui import Ui
from twoinst import Checks, start


def mode(con):
    return con.call('game.mode')['out']


def vote(con):
    return con.call('coop.vote')['out']


def counter(con, name):
    m = re.search(name + r' (\d+)', vote(con))
    return int(m.group(1)) if m else -1


def wait_connected(con, t=40):
    for _ in range(t):
        if 'connected' in con.call('coop.status')['out'].splitlines()[0]:
            return True
        time.sleep(1)
    return False


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    gu = Ui(guest, 'guest')
    print(' ', host.call(f'coop.fix core.vote_scope {"off" if without else "on"}')['out'].strip())
    time.sleep(2)

    # 1. a vote from the previous session must not open the editor in the next one
    m0 = mode(host)
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    time.sleep(2)
    ck('vote active' in vote(host), 'the editor request started a vote')
    print(' ', host.call('coop.leave')['out'].strip())
    time.sleep(1)
    print(' ', host.call(lane.HOST_CMD)['out'].strip())
    time.sleep(6)
    m1 = mode(host)
    print('  host mode after hosting again:', m1)
    ck(m1 == m0, 'the old vote did not open the editor in the new session')
    ck('vote none' in vote(host), 'no vote is left over')
    if m1 != m0:  # the old code opened the editor: back to the stage for part 2
        host.call('click 1416 1062')
        for _ in range(30):
            if mode(host) == m0:
                break
            time.sleep(1)
    guest.call(lane.JOIN)
    ck(wait_connected(guest), 'the guest joined again')
    time.sleep(3)
    print(' ', host.call('coop.vote test')['out'].strip().splitlines()[0])
    st = gu.wait(lambda s: s['open'] == '1' and 'wants to evolve' in s.get('world', ''), 15, 'vote on the guest')
    ck(st is not None, 'a new vote starts and reaches the guest')
    host.call('coop.vote go')
    time.sleep(2)

    # 2. an action vote must not run after the host left its stage
    ran0 = counter(host, 'actions run')
    print(' ', host.call('coop.vote action')['out'].strip().splitlines()[0])
    st = gu.wait(lambda s: s['open'] == '1' and 'test an action' in s.get('world', ''), 15, 'action vote on the guest')
    ck(st is not None, 'the guest sees the action vote')
    print(' ', host.call('coop.menu')['out'].strip())
    in_menu = False
    for _ in range(90):
        if 'GalaxyGameEntry' in mode(host):
            in_menu = True
            break
        time.sleep(1)
    ck(in_menu, 'the host left the stage for the galaxy menu')
    time.sleep(2)
    gu.click('ready')  # the guest's "I'm ready" (the old code then ran the action in the menu)
    time.sleep(4)
    ran = counter(host, 'actions run')
    print(f'  actions run {ran0} -> {ran}; {vote(host).splitlines()[0]}')
    ck(ran == ran0, 'the action of the vote did not run after the stage was left')
    ck('vote none' in vote(host), 'the vote ended with its stage')

    host.call('coop.fix core.vote_scope on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
