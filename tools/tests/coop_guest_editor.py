#!/usr/bin/env python3
"""Every player evolves its own creature: after the host's evolution vote the guest opens the
creature editor for its own creature (a creation from its My Creations). The guest's "done" must
leave the editor, save the edited creature and make it the guest's own (and the host's view of it).

  coop_guest_editor.py [--keep]    (--keep: stop with both games in their editors, for debugging)
"""
import re
import sys
import time

import creations_state
from coop_ui import Ui
from twoinst import Checks, shot, start

DONE = (1416, 1062)    # the green "done" check of the creature editor (1440x1080 test window)
# an edit that costs no DNA: move the white bow on the back of the guest's Kirgle (fixture
# creations-guest) a bit up
EDIT = (1110, 480, 1060, 420)


def mode(con):
    return con.call('game.mode')['out']


def own(con, who='mine'):
    m = re.search(who + r' creature: ([0-9A-F]{8})', con.call('coop.creations')['out'])
    return m.group(1) if m else None


def main():
    keep = '--keep' in sys.argv
    host, guest = start()
    ck = Checks()
    gu = Ui(guest, 'guest')
    time.sleep(2)
    m0 = mode(host)
    # BUG-0345: the guest plays its own creature (the fixture's Kirgle, the one EDIT moves a part of) only when
    # chosen in a world it never played it in; the profile is put back as it was at the end
    st0 = creations_state.save(guest)
    mine = creations_state.choose_own(guest)
    print('  guest profile before:', st0, '| chose', mine, flush=True)
    t0 = time.time()
    while mine and time.time() - t0 < 30:
        gav = re.search(r'key=0x([0-9A-F]{8})', guest.call('coop.avatar')['out'])
        if own(guest) == mine and gav and gav.group(1) == mine:
            break
        time.sleep(1)
    try:
        return run(host, guest, ck, gu, m0, keep)
    finally:
        creations_state.restore(guest, st0)  # the profile as it was (BUG-0345)


def run(host, guest, ck, gu, m0, keep):
    g_before = own(guest)
    print('  guest own creature before:', g_before)

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
    shot(guest, 'guest_editor_open.png')
    if keep:
        print('  both games are in their editors (--keep)')
        return 0

    guest.call('drag %d %d %d %d' % EDIT)
    guest.call('wait.frames 30 10000')
    shot(guest, 'guest_editor_edited.png')
    # the host leaves its editor first (the world goes on), then the guest presses done
    host.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(host) != m0:
        time.sleep(1)
    ck(mode(host) == m0, 'the host is back in its stage')
    guest.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(guest) != m0:
        time.sleep(1)
    shot(guest, 'guest_editor_done.png')
    ck(mode(guest) == m0, f'the guest left its editor with "done" ({mode(guest)})')
    w = re.search(r'part list waived (\d+)', guest.call('coop.vote')['out'])
    print('  part-list errors waived in the own editor:', w and w.group(1))

    # back in the stage: the edited creature is the guest's own and everybody sees it
    t0 = time.time()
    g_now = None
    while time.time() - t0 < 90:
        g_now = own(guest)
        if g_now and g_now != g_before and guest.call('coop.avatar')['ok']:
            break
        time.sleep(2)
    print(f'  guest own creature {g_before} -> {g_now}')
    ck(g_now and g_now != g_before, 'the edited creature is a new creation and the guest\'s own')
    # (a host world that arrived meanwhile is loaded now, then the own body is set again)
    t0 = time.time()
    gav = None
    while time.time() - t0 < 90:
        gav = re.search(r'key=0x([0-9A-F]{8})', guest.call('coop.avatar')['out'])
        if gav and gav.group(1) == own(guest):
            break
        time.sleep(2)
    ck(gav and gav.group(1) == own(guest), f'the guest plays it ({gav and gav.group(1)})')
    t0 = time.time()
    ok = False
    while time.time() - t0 < 60 and not ok:
        hp = re.search(r"remote 1 'Bob'.*\n.*species=([0-9A-F]{8})", host.call('coop.status')['out'])
        hk = own(host, 'player 1')
        ok = bool(hp and hk and hp.group(1) == hk)
        time.sleep(2)
    ck(ok, 'the host shows the guest with the edited creature')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
