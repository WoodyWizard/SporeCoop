#!/usr/bin/env python3
"""A guest's own Creature editor has its red cross and leaves with it (BUG-0756).

Creature fixture. With creations.editor_request (BUG-0720) the guest's own editor got the game's evolution request
flags and with them the evolution editor's interface: no red cross, no save dialog, no way to leave without the edit.
The owner wants the 0.7.10 own editor back (the cross, Save New / Replace / Don't Save):

  1. The host evolves (`coop.vote evolve`), the guest takes "Evolve now": its own editor opens.
  2. The guest's game shows the red cross: the editor's window 0x90439D7C (shown by cEditor::OnEnter from the
     request's +0x3B, docs/re/creations.md#creations-own-editor-cancel) is visible where the cross is drawn.
  3. One click on the red cross leaves the unchanged editor: the guest plays its creature again.

  coop_own_editor_cancel.py                                           (PASS)
  SPORE_FIX_OFF=creations.own_editor_cancel coop_own_editor_cancel.py  (steps 2 and 3 FAIL)
"""
import time

from coop_ui import Ui
from twoinst import Checks, shot, start

DONE = (1416, 1062)    # the creature editors' green check (1440x1080 test window)
CANCEL = (1383, 1062)  # the red cross
CROSS_WIN = '90439D7C'


def mode(con):
    return con.call('game.mode')['out']


def in_editor(con):
    return 'Editor' in mode(con)


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def back(con, m0, secs=60):
    return wait(lambda: mode(con) == m0 and con.call('coop.avatar')['ok'], secs)


def cross(con):
    """the cross window's screen rect, or None when it is not shown"""
    out = con.call(f'coop.ui where {CROSS_WIN}')['out'].split()
    return None if len(out) < 5 or out[1] == 'none' else [float(v) for v in out[1:5]]


def main():
    host, guest = start()
    ck = Checks()
    gu = Ui(guest, 'guest')
    time.sleep(3)
    m0 = mode(host)

    # 1. the guest's own editor
    print(' ', host.call('coop.vote evolve')['out'].strip().splitlines()[0], flush=True)
    st = gu.wait(lambda s: s.get('open') == '1' and s['ready']['vis'], 20, 'the offer')
    if st:
        gu.click('ready')
    ck(wait(lambda: in_editor(guest), 90), f"the guest's own editor opens ({mode(guest).strip()})")
    time.sleep(8)  # (its interface comes up)
    shot(guest, 'own_editor_cancel.png')

    # 2. the red cross is shown
    r = wait(lambda: cross(guest), 10)
    ck(r is not None and r[0] <= CANCEL[0] <= r[2] and r[1] <= CANCEL[1] <= r[3],
       f"the guest's own editor shows its red cross (window {CROSS_WIN} at {r})")

    # 3. one click on it leaves the unchanged editor
    guest.call('click %d %d' % CANCEL)
    left = back(guest, m0, 20)
    ck(left, f"one click on the red cross leaves the unchanged editor ({mode(guest).strip()})")
    if not left:  # (without the cross: done)
        guest.call('click %d %d' % DONE)
        back(guest, m0)
    if in_editor(host):
        host.call('click %d %d' % DONE)
    back(host, m0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
