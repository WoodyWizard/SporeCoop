#!/usr/bin/env python3
"""A host that leaves the session right after the save for a world share was posted can still
leave its stage (BUG-0140): the share entry stayed in world's queue for good, save_busy() stayed
true and every quit message (0x4715068) was posted again, so the game never left the stage.

  1. the guest joins again: the host saves for the share; while "saved, being written" shows,
     the host presses Leave (coop.leave)
  2. the host leaves the stage for the galaxy menu (coop.menu posts the quit message): the game's
     own mode must reach GalaxyGameEntry

  coop_leave_during_share.py [--without-fix]   (--without-fix: coop.fix core.share_reset off, must FAIL)"""
import re
import sys
import time

import lane
from twoinst import Checks, start


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    print(' ', host.call(f'coop.fix core.share_reset {"off" if without else "on"}')['out'].strip().replace('\n', ', '))
    time.sleep(3)
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    saved = False
    t0 = time.time()
    while time.time() - t0 < 60:
        if 'saved, being written' in host.call('coop.status')['out']:
            saved = True
            break
        time.sleep(0.2)
    ck(saved, f'the host saved for the share ({time.time() - t0:.1f} s after the join)')
    print(' ', host.call('coop.leave')['out'].strip())
    time.sleep(1)
    st = host.call('coop.status')['out']
    print('  host status:', st.splitlines()[0], '|', ' '.join(l for l in st.splitlines() if l.startswith('share for')))

    print(' ', host.call('coop.menu')['out'].strip())
    in_menu = None
    t0 = time.time()
    while time.time() - t0 < 40:
        if 'GalaxyGameEntry' in host.call('game.mode')['out']:
            in_menu = time.time() - t0
            break
        time.sleep(1)
    held = re.search(r'quit held for a save (\d+)', host.call('coop.vote')['out'])
    print(f'  galaxy menu after {in_menu and round(in_menu)} s, quit held {held and held.group(1)} times')
    ck(in_menu is not None, "the host's game left the stage for the galaxy menu")

    host.call('coop.fix core.share_reset on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
