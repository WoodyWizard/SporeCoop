#!/usr/bin/env python3
"""BUG-0924 (tests): a key frame is not shot while a game stands in a pause, a cinematic, an editor or a load.

The 0.7.12 final checks had seven key frames of games in an editor, a cinematic or the pause menu: their notes could
not be judged. twoinst.keyframe now waits for the games to settle (at most SPORE_KEYFRAME_SETTLE s, default 20).
Here the guest opens its pause menu (Esc: the game's time stands, `busy: paused`) and closes it 6 s later from
another thread; a key frame is asked for right after the menu opened. Checked: the frame was shot after the menu
closed (the manifest's time against the close), the guest's game was in play then (not paused), and the manifest says
how long it waited. Before the fix of twoinst the frame was shot at once, with the pause menu in the picture.

  SPORE_LANE=4 python3 tools/tests/coop_keyframe_settle.py
"""
import re
import threading
import time

from twoinst import Checks, keyframe, start  # (first: it puts tools/ on the path)
import lane
from sporectl import Console


def busy(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1) if m else '?'


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(2)
    print(f'  before: host busy {busy(host)}, guest busy {busy(guest)}', flush=True)
    guest.call('key esc')
    t0 = time.time()
    while time.time() - t0 < 5 and 'paused' not in busy(guest):
        time.sleep(0.2)
    b = busy(guest)
    ck('paused' in b, f'the guest\'s pause menu stands its game ({b})')
    closed = []

    def close_later():
        c = Console(lane.GUEST)
        time.sleep(6)
        c.call('key esc')
        closed.append(time.time())

    th = threading.Thread(target=close_later, daemon=True)
    th.start()
    e = keyframe('settle', 'both games in Creature play: no pause menu, no blurred picture')
    th.join(15)
    t_close = closed[0] if closed else None
    print(f'  the pause menu closed at +{t_close and round(t_close - t0, 1)} s, the frame entry at '
          f'+{e and round(e["t"] - t0, 1)} s, extra {e and e.get("extra")}', flush=True)
    ck(e is not None and all(g.get('ok') for g in e['games'].values()), 'the key frame shot both games')
    ck(e is not None and t_close is not None and e['t'] >= t_close,
       'the key frame waited for the guest\'s pause menu to close')
    ck(e is not None and any('waited' in x for x in e.get('extra', [])), 'the manifest says the frame waited')
    t2 = time.time()
    b = busy(guest)
    while time.time() - t2 < 5 and 'paused' in b:
        time.sleep(0.3)
        b = busy(guest)
    ck('paused' not in b, f'the guest plays again after the menu closed ({b})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
