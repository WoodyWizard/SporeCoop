#!/usr/bin/env python3
"""BUG-0368: a guest's own creature editor (opened after the host's evolution vote) must lock the
parts the species has not collected yet, as the host's evolution editor does. Player report
(2026-11-01): the guest had "every single customization unlocked since the beginning of creature
stage".

The game's palette shows a part locked only when the editor's request carries the collected parts
(cPlayer +0x10E8, a cCollectableItems, at request +0x94; the palette's check 0x5C6820 treats a
request without it as "every part available"). Checked on the guest while its own editor is open:
`coop.editor` reads the open editor's request (the cEditor's +0x1CC) and applies the palette's rule
to every unlockable part of the collected parts: the number the palette shows locked must be > 0
(the fixture's species has collected few parts), and "done" still leaves the editor.

Runs that must FAIL: SPORE_FIX_OFF=creations.own_editor_items (the request without the collected
parts: locked 0, every part offered)."""
import re
import time

from coop_ui import Ui
from twoinst import Checks, shot, start

DONE = (1416, 1062)    # the green "done" check of the creature editor (1440x1080 test window)


def mode(con):
    return con.call('game.mode')['out']


def editor(con):
    out = con.call('coop.editor')['out'].strip()
    m = re.search(r'editor ([0-9A-F]{8}) key ([0-9A-F]{8}) request \S+ items (\S+) collected (\S+) '
                  r'unlockable (\d+) locked (\d+)', out)
    return out, m


def null(p):
    return p in ('00000000', '0x00000000', '(nil)', '0', 'nil')


def main():
    host, guest = start()
    ck = Checks()
    gu = Ui(guest, 'guest')
    time.sleep(2)
    m0 = mode(host)

    print(' ', host.call('coop.vote editor')['out'].strip())
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 60 and (mode(host) == m0 or mode(guest) == m0):
        time.sleep(1)
    ck(mode(guest) != m0, f"the guest's own editor opened ({mode(guest)})")
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    shot(guest, 'guest_editor_locks.png')
    shot(host, 'host_editor_locks.png')

    gout, g = editor(guest)
    hout, _ = editor(host)
    print('  guest:', gout)
    print('  host: ', hout)
    ck(g is not None, f"the guest's open editor and its request are found ({gout[:120]})")
    if g:
        unlockable, locked = int(g.group(5)), int(g.group(6))
        ck(unlockable > 0, f"the guest's game has unlockable creature parts ({unlockable})")
        ck(not null(g.group(3)) and g.group(3) == g.group(4),
           f"the guest's editor got the species' collected parts (items {g.group(3)}, collected {g.group(4)})")
        ck(locked > 0, f"the guest's palette shows {locked} of {unlockable} unlockable parts locked")

    # done without an edit leaves at once (the request's extra field changes nothing there)
    host.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(host) != m0:
        time.sleep(1)
    guest.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(guest) != m0:
        time.sleep(1)
    ck(mode(guest) == m0, f'the guest left its editor with "done" ({mode(guest)})')
    ck(mode(host) == m0, f'the host is back in its stage ({mode(host)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
