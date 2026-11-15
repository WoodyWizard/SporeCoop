#!/usr/bin/env python3
"""The mod's watcher of the game's "game saved" box (world.cpp box_task) against what the game shows.

  --case question   BUG-0846: the pause menu's "Do you want to save your game?" uses the saved box's window
                    (0x06318E78). A save of ours made while it is up must leave the question to the player (still
                    open 10 s later, the game still in the stage) and still confirm the real saved box. And a
                    second save while the first one's box is held up is refused. SPORE_FIX_OFF=world.saved_box_only
                    fails (the mod's Enter answers the question, the real box is left up).
  --case cursor     BUG-0804: after the mod's click on the box's OK the game reads the real mouse again within
                    1 s (the `cursor` command: GetCursorPos as the game gets it). SPORE_FIX_OFF=world.saved_box_cursor
                    fails (the game reads the OK's point for ~5 s).
  --case video      BUG-0806: the game's video mode goes to 800x600 (Renderer_SetVideoMode, as its options do); the
                    saved box keeps its 1440x1080 place, its OK (959..1001, 589..631) outside the 800x600 window. The
                    box must be confirmed within 10 s. SPORE_FIX_OFF=world.saved_box_offscreen fails (the click at the
                    OK's point hits nothing, the box stays up). (A box only moved past the edge with coop.ui area is
                    still hit by the click under Wine: not a reproduction.)

  SPORE_LANE=N python3 tools/tests/coop_saved_box.py --case question|cursor|video
"""
import argparse
import re
import sys
import time

import uifind
from twoinst import Checks, start

QUESTION = 'Do you want to save your game?'
SAVED = 'is saved.'


def mode(con):
    return con.call('game.mode')['out'].split()[1]


def where(con, hexid):
    """screen rect of the first visible window with this control id, or None"""
    m = re.search(hexid + r' (-?\d+) (-?\d+) (-?\d+) (-?\d+)', con.call(f'coop.ui where {hexid}')['out'])
    return tuple(int(m.group(i)) for i in range(1, 5)) if m else None


def saved_up(con):
    return bool(uifind.shown(con, SAVED))


def wait(pred, secs, step=0.3):
    t0 = time.time()
    while time.time() - t0 < secs:
        if pred():
            return True
        time.sleep(step)
    return pred()


def close_all(con):
    """leave the question, the pause menu and any saved box (for the next test on warm games)"""
    for _ in range(6):
        if uifind.shown(con, QUESTION):
            uifind.press(con, 'Cancel', timeout=4)
        elif saved_up(con):
            ok = where(con, 'FFFFFFF1')
            if ok and ok[2] < 1440:
                con.call(f'click {(ok[0] + ok[2]) // 2} {(ok[1] + ok[3]) // 2}')
            else:
                con.call('key enter')
        elif uifind.shown(con, 'Resume Game'):
            con.call('key esc')
        else:
            return
        time.sleep(1.5)


def case_question(ck, h):
    h.call('key esc')
    ck(wait(lambda: uifind.shown(h, 'Resume Game'), 6), 'the pause menu is open')
    ck(uifind.press(h, 'Exit to Main Menu', QUESTION, timeout=8), 'the game asks "Do you want to save your game?"')
    time.sleep(1)
    ck('saved' in h.call('coop.save')['out'], 'the host saves (a guest joining while the host is in that question)')
    t0 = time.time()
    saved_seen = saved_closed = False
    q_gone_at = None
    while time.time() - t0 < 12:
        q = bool(uifind.shown(h, QUESTION))
        s = saved_up(h)
        saved_seen |= s
        if saved_seen and not s:
            saved_closed = True
        if not q and q_gone_at is None:
            q_gone_at = time.time() - t0
        time.sleep(0.4)
    still = bool(uifind.shown(h, QUESTION))
    m = mode(h)
    print(f'  question gone at {q_gone_at}, saved box seen {saved_seen} closed {saved_closed}, mode {m}', flush=True)
    ck(still and m == 'Creature', f'the question is still open 12 s after the save, the game in its stage '
                                  f'(the mod did not answer it; open {still}, {m})')
    ck(saved_seen and saved_closed, f'the real saved box came up and the mod confirmed it (seen {saved_seen}, '
                                    f'closed {saved_closed})')
    ck(not saved_up(h), 'no saved box left up (the game is not paused under it)')
    close_all(h)
    ck(wait(lambda: not uifind.shown(h, QUESTION) and not uifind.shown(h, 'Resume Game'), 8) and mode(h) == 'Creature',
       'the player cancels the question and the pause menu: back in the game')
    # a second save while the first one's box is still up would take that box for its own confirmation
    ck('saved' in h.call('coop.save hold')['out'], 'a save whose box is held up (coop.save hold)')
    ck(wait(lambda: saved_up(h), 15), 'its saved box is up')
    time.sleep(1)
    second = h.call('coop.save')['out']
    print(f'  second save under the box: {second.strip()}', flush=True)
    ck('previous save' in second, f'a second save is refused while the first one\'s box is up ({second.strip()})')
    h.call('coop.save confirm')
    ck(wait(lambda: not saved_up(h), 10), 'the held box is confirmed')


def case_cursor(ck, h):
    ck('saved' in h.call('coop.save hold')['out'], 'the host saves (box held for the measurement)')
    ck(wait(lambda: saved_up(h), 15), 'the saved box is up')
    ok = where(h, 'FFFFFFF1')
    ck(ok is not None, f'its OK button is shown ({ok})')
    if not ok:
        return
    cx, cy = (ok[0] + ok[2]) // 2, (ok[1] + ok[3]) // 2
    h.call('coop.save confirm')
    ck(wait(lambda: not saved_up(h), 10, 0.1), 'the mod confirmed the box (it closed)')
    t_closed = time.time()
    samples = []
    while time.time() - t_closed < 7:
        m = re.search(r'virtual=(\d) x=(-?\d+) y=(-?\d+)', h.call('cursor')['out'])
        if m:
            samples.append((time.time() - t_closed, int(m.group(1)), int(m.group(2)), int(m.group(3))))
        time.sleep(0.2)
    at_ok = [t for t, v, x, y in samples if abs(x - cx) <= 3 and abs(y - cy) <= 3]
    held = max(at_ok) if at_ok else 0.0
    print(f'  OK at ({cx}, {cy}); {len(samples)} samples, the game read the OK point until {held:.1f} s after the box '
          f'closed; first {samples[:2]}, last {samples[-1:]}', flush=True)
    ck(samples, 'the cursor command answers')
    ck(held < 1.0, f'the game reads the real mouse again within 1 s after the box closed (OK point read until {held:.1f} s)')


def set_mode(con, w, h):
    """the game's own video mode change (as its options do; coop_window_layout.py)"""
    r = re.search(r'eax=0x([0-9A-F]+)', con.call('call Graphics::IRenderer::Get cdecl')['out'])
    r = int(r.group(1), 16) if r else 0
    full = con.call(f'mem.read 0x{r + 0x6C:X} 1')['out'].split()
    fs = int(full[1], 16) if len(full) > 1 else 0
    print('  ', con.call(f'call SporeCoop::Renderer_SetVideoMode thiscall 0x{r:X} {fs} {w} {h} -1')['out'].strip(), flush=True)
    con.call('wait.frames 60 30000')


def case_video(ck, h):
    """the fork's way (BUG-0806): the window goes to 800x600, the box comes up past its right edge"""
    set_mode(h, 800, 600)
    try:
        st = re.search(r'^screen (\d+) (\d+)', h.call('coop.uistate')['out'], re.M)
        print(f'  screen now {st.group(0) if st else "?"}', flush=True)
        ck('saved' in h.call('coop.save')['out'], 'the host saves at 800x600')
        ck(wait(lambda: saved_up(h), 15, 0.2), 'the saved box is up')
        ok = where(h, 'FFFFFFF1')
        print(f'  OK at {ok}', flush=True)
        closed = wait(lambda: not saved_up(h), 10, 0.3)
        ck(closed, f'the box is confirmed within 10 s (OK at {ok})')
    finally:
        close_all(h)
        set_mode(h, 1440, 1080)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('question', 'cursor', 'video'), required=True)
    a = ap.parse_args()
    h, g = start('creature')
    ck = Checks()
    try:
        {'question': case_question, 'cursor': case_cursor, 'video': case_video}[a.case](ck, h)
    finally:
        close_all(h)
    ck(mode(h) == 'Creature', f'the host is in its game at the end ({mode(h)})')
    ck.faults(host=h, guest=g)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
