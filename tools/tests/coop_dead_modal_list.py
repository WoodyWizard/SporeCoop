#!/usr/bin/env python3
"""BUG-0956: a destroyed window in the window manager's list of modal windows must not be made the modal one.

A guest that had earned a Space badge and joined again from the same game crashed while it loaded the host's world
(9 of 10 runs, no crash report): the mode switch after the load popped the modal window (cWindowManager PopModal
0x959410, WM vt 0x78), PopModal made the next window of the list +0x81C the modal one and called its vt 0x10, and
that window was already destroyed (vtable 0x13EB92C, slot 0x10 = 0x00420065: `mov esp,ebp; pop ebp; ret 4`, the
game ran on with esp in the heap). This test puts that state in directly on the guest in Space (`coop.deadmodal
list`: a destroyed object listed under a modal window, its vtable slots 0x65) and pops the modal window through the
game's own PopModal. The window manager must end with no modal window and nothing listed, the game must not fault.

  coop_dead_modal_list.py   (SPORE_FIX_OFF=game.dead_modal_list: PopModal calls 0x65, the game faults: FAIL)"""
import re
import time

from twoinst import Checks, start


def dropped(con):
    m = re.search(r'dead listed modal windows dropped (\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    d0 = dropped(guest)
    try:
        out = guest.call('coop.deadmodal list')['out'].strip()
    except OSError as e:
        out = f'the guest\'s console is gone ({e})'
    print('  deadmodal list:', out, flush=True)
    ck('nothing listed' in out, f'PopModal left the window manager with no modal window and nothing listed ({out})')
    try:
        guest.call('wait.frames 30 10000')
        d1 = dropped(guest)
        exc = guest.call('exc')['out']
    except OSError:
        d1, exc = -1, ''
    ck(d0 >= 0 and d1 == d0 + 1, f'the guard dropped the destroyed listed window once ({d0} -> {d1})')
    ck('total faults: 0' in exc, 'no fault in the game\'s UI code')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
