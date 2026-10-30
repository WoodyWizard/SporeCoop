#!/usr/bin/env python3
"""BUG-0270: a destroyed modal window left in the game's window manager must not fault the next
mouse message.

A guest that loaded a shared Space world after a forced leave to the galaxy menu faulted in the
game's UI (0x959049 calling 0x65: cWindowManager::UpdateHover through the modal window at WM+0x82C,
an object the game had already destroyed). How the game came to leave a destroyed window there is
not known, so this test puts the same state in directly (`coop.deadmodal`: WM+0x82C points to an
object whose vtable slots hold 0x65, as the destroyed one's did) on the guest in Space and moves the
mouse over the game window. The game's own exception counter (`exc`) must stay at 0 and the game
must keep handling the UI (a click on the co-op window still works afterwards).

  coop_dead_modal.py   (SPORE_FIX_OFF=game.dead_modal: the fault at 0x65 comes back, it FAILS)"""
import re
import time

from twoinst import Checks, start


def dropped(con):
    m = re.search(r'dead modal windows dropped (\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    e0 = guest.call('exc')['out'].splitlines()[0]
    print('  guest before:', e0)
    d0 = dropped(guest)  # warm runs: the counter keeps counting
    out = guest.call('coop.deadmodal')['out'].strip()
    print('  deadmodal:', out)
    ck('destroyed object' in out, f'the window manager holds a destroyed modal window ({out})')
    for i in range(6):  # mouse messages: cWindowManager::Update -> ProcessMessage -> UpdateHover
        guest.call(f'move {300 + 40 * i} {300 + 25 * i}')
        time.sleep(0.3)
    guest.call('wait.frames 30 10000')
    d1 = dropped(guest)
    ck(d0 >= 0 and d1 == d0 + 1, f'the guard dropped the destroyed modal window once ({d0} -> {d1})')
    ck('total faults: 0' in guest.call('exc')['out'], 'no fault in the game\'s UI code')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
