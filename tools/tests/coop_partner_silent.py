#!/usr/bin/env python3
"""BUG-0513: a partner whose game stalls or reports itself deep under the ground (Creature, host's screen).

Player report (0.7.3.6, host): "guest is bugged into the ground in my pov". The guest's game stopped drawing for
17-38 s at a time (minimized / alt-tabbed, a stall while it received a world); in its own trace its creature was
6.5 m under its ground while paused, and the host drew the puppet 3-6 m under its ground (agl -3.2 .. -6.3), then
hid it after 5 s without a word.

The puppet deep under the ground is suite puppet_deep (coop_puppet_walk.py with `coop.track lift -6`: standing,
the game keeps a puppet on its ground by itself, only a walking one went under).
The guest's game stalls (`frame.limit 9000`: a frame every 9 s, its states stop) for ~16 s: the host's screen
   shows a notice that the guest is not responding (a toast in `coop.status`), and another when it is back.

  coop_partner_silent.py   (SPORE_FIX_OFF=core.partner_silent: FAILS)"""
import re
import time

from twoinst import Checks, start


def toasts(con):
    return re.findall(r"^toast \w+ \([^)]*\) '([^']*)'", con.call('coop.status')['out'], re.M)


def main():
    host, guest = start('creature')
    ck = Checks()
    p = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out'])
    guest.call('coop.npc gopos %s %s %s' % p.groups())
    time.sleep(3)

    # 2. the guest's game stalls
    time.sleep(2)
    guest.call('frame.limit 9000')
    silent = back = False
    t0 = time.time()
    while time.time() - t0 < 16:
        silent = silent or any('not responding' in t for t in toasts(host))
        time.sleep(0.5)
    guest.call('frame.limit auto')
    t0 = time.time()
    while time.time() - t0 < 12 and not back:
        back = any('responding again' in t for t in toasts(host))
        time.sleep(0.5)
    ck(silent, 'the host is told that the guest is not responding while its game stalls')
    ck(back, 'the host is told when the guest responds again')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
