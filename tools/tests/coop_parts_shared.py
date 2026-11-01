#!/usr/bin/env python3
"""BUG-0401: the creature parts a player finds are the party's (shared progress, like DNA): a part the
host finds is unlocked in every guest's game, a part a guest finds in the host's (and so in every
other guest's), and a rejoin keeps them all. Nothing is ever locked.

Checked in the games' own collected parts (cPlayer +0x10E8, `coop.parts`: the count of unlocked
unlockable parts and an order-free hash of their keys, the trace's parts= field) and in the guest's
own editor palette (`coop.editor`: the parts its palette shows locked, BUG-0368's machinery):
  1. the host finds a part the game's way (`coop.parts find <level>`: the creature stage's "a part was
     found" message 0x5371F11, a skeleton's reward; `unlock 2` if no level gives one): the guest's
     collected parts become the host's (count and hash) within 20 s;
  2. the guest finds one: the host gets it (both the same again);
  3. the guest's own editor (the host's vote) shows exactly the party's parts unlocked, and still
     locks the rest (BUG-0368: not every part);
  4. the guest leaves and joins again (the host's world is saved and loaded again): the guest still
     has every part of the party.

Runs that must FAIL: SPORE_FIX_OFF=parts.shared (each game keeps its own parts: 1 and 2 time out, the
rejoin loses the guest's find)."""
import re
import time

import lane
from coop_ui import Ui
from twoinst import Checks, shot, start, wait_guest_in

DONE = (1416, 1062)    # the green "done" check of the creature editor (1440x1080 test window)


def parts(con):
    out = con.call('coop.parts')['out']
    m = re.search(r'parts=(\d+):([0-9A-F]{8}) unlockable=(\d+)', out)
    return (int(m.group(1)), m.group(2), int(m.group(3))) if m else (None, None, None)


def sync_line(con):
    m = re.search(r'^sync: (.*)$', con.call('coop.parts')['out'], re.M)
    return m.group(1) if m else '?'


def find(con, who):
    """the game's own 'a part was found'; falls back to UnlockPart when no level gives a part"""
    for level in (1, 2, 3, 4, 5, -1):
        out = con.call(f'coop.parts find {level}')['out']
        m = re.search(r'unlocked_now=(-?\d+)', out)
        if m and int(m.group(1)) > 0:
            print(f'  {who} found {m.group(1)} part(s) the game\'s way (level {level}): {out.splitlines()[0]}', flush=True)
            return 'find'
    out = con.call('coop.parts unlock 2')['out']
    print(f'  {who}: no level gave a part, UnlockPart instead: {out.splitlines()[0]}', flush=True)
    return 'unlock'


def wait_same(a, b, t=20):
    t0 = time.time()
    pa = pb = None
    while time.time() - t0 < t:
        pa, pb = parts(a), parts(b)
        if pa[0] is not None and pa[:2] == pb[:2]:
            return True, pa, pb, time.time() - t0
        time.sleep(1)
    return False, pa, pb, time.time() - t0


def mode(con):
    return con.call('game.mode')['out']


def editor_locked(con):
    out = con.call('coop.editor')['out'].strip()
    m = re.search(r'unlockable (\d+) locked (\d+)', out)
    return out, (int(m.group(1)), int(m.group(2))) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'the guest is in the host world')
    h0, g0 = parts(host), parts(guest)
    print(f'  start: host {h0}, guest {g0}', flush=True)
    ck(h0[0] is not None and g0[0] is not None, 'both games have collected parts')
    ok, _, _, _ = wait_same(host, guest, 15)
    ck(ok, f'both start with the same parts (host {parts(host)}, guest {parts(guest)})')

    # 1. the host finds a part
    find(host, 'host')
    h1 = parts(host)
    ck(h1[0] > h0[0], f'the host has more parts now ({h0[0]} -> {h1[0]})')
    ok, ph, pg, dt = wait_same(host, guest)
    print(f'  after {dt:.0f} s: host {ph}, guest {pg} | guest sync: {sync_line(guest)}', flush=True)
    ck(ok, f"the guest's game has the parts the host found (guest {pg[0]}:{pg[1]}, host {ph[0]}:{ph[1]})")

    # 2. the guest finds a part
    g_before = parts(guest)
    find(guest, 'guest')
    g2 = parts(guest)
    ck(g2[0] > g_before[0], f'the guest has more parts now ({g_before[0]} -> {g2[0]})')
    ok, ph, pg, dt = wait_same(host, guest)
    print(f'  after {dt:.0f} s: host {ph}, guest {pg} | host sync: {sync_line(host)}', flush=True)
    ck(ok and ph[0] >= g2[0], f"the host's game has the parts the guest found (host {ph[0]}:{ph[1]}, guest {pg[0]}:{pg[1]})")
    # the party's parts: the host's after its find plus the guest's find (a random locked part: the same
    # part as the host's is a 1 in ~250 chance)
    union = h1[0] + (g2[0] - g_before[0])
    print(f'  the party has {union} parts', flush=True)

    # 3. the guest's own editor offers the party's parts, and locks the rest (BUG-0368)
    gu = Ui(guest, 'guest')
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
    shot(guest, 'parts_shared_guest_editor.png')
    out, lk = editor_locked(guest)
    print('  guest editor:', out, flush=True)
    ck(lk is not None, f"the guest's open editor is found ({out[:100]})")
    if lk:
        unlockable, locked = lk
        ck(locked == unlockable - union,
           f"the guest's palette offers the party's {union} parts: {unlockable - locked} of {unlockable} available")
        ck(locked > 0, f'the guest\'s palette still locks the parts nobody found ({locked}, BUG-0368)')
    host.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(host) != m0:
        time.sleep(1)
    guest.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(guest) != m0:
        time.sleep(1)
    ck(mode(guest) == m0 and mode(host) == m0, f'both left the editor ({mode(host)} / {mode(guest)})')
    time.sleep(3)

    # 4. a rejoin: the host's world is saved and loaded by the guest again
    print(' ', guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(3)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), "the guest is back in the host's world")
    time.sleep(20)
    wait_guest_in(host, guest)
    ok, ph, pg, dt = wait_same(host, guest, 40)
    print(f'  after the rejoin: host {ph}, guest {pg} | guest sync: {sync_line(guest)}', flush=True)
    ck(ok and pg[0] is not None and pg[0] >= union,
       f"after the rejoin the guest has every part of the party ({pg[0]} of {union}, host {ph[0]})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
