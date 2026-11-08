#!/usr/bin/env python3
"""Galaxy menu: Escape and "Co-op" clicks around the menu intro keep the game answering (BUG-0184).

Once in 15 runs of coop_ui.py (2026-10-24) the host's console timed out right after its click on "Co-op" during the
menu intro: the mod posts WM_KEYDOWN/UP Escape to skip the intro (coop_ui.cpp, docs/re/space.md 7.9), the game
handles it a frame or more later. Candidate (BUG-0184, 2026-10-30): an Escape that reaches the galaxy menu after the
intro (or before it starts) opens something modal that stops the frames. One game, alone, in the galaxy menu:

  A  Escape with no intro playing, 3 times: frames go on, the console answers, no window of the game stays up
  B  the menu's own intro again (it starts each time the menu comes back: a load, then the quit to the menu), then
     "Co-op" clicked 0 / 0.3 / 1 / 3 / 6 / 8.5 s into it (the mod's Escape skip, also near the intro's end, the late
     Escape of the candidate), or Escape at its start and 9.5 s into it: every round the frames go on, the co-op
     window opens after a click and the intro is over; the window is closed for the next round
  C  the menu's own intro (the first of the session) with a click at its first frames

  SPORE_LANE=9 python3 tools/tests/coop_menu_escape.py
"""
import re
import sys
import time

import fixtures
import lane
from coop_ui import Ui, wait_up
from sporectl import Console

MENU = 'GalaxyGameEntry'


def answers(con, what):
    """the game draws frames and answers (a modal loop or a hang stops the frames)"""
    t0 = time.time()
    try:
        ok = con.call('wait.frames 30 8000')['ok'] and con.call('ping')['ok']
    except OSError as e:
        print(f'  {what}: console error {e!r}', flush=True)
        return False
    print(f'  {what}: 30 frames in {time.time() - t0:.2f} s' if ok else f'  {what}: NO frames in 8 s', flush=True)
    return ok


def intro(con):
    return 'menu intro)' in con.call('coop.status')['out']


def wait(f, t):
    t0 = time.time()
    while time.time() - t0 < t:
        if f():
            return True
        time.sleep(0.1)
    return False


def main():
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
        ok &= bool(cond)

    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.sh('fixture', 'restore', 'creature', lane.HOST_PROFILE)
    lane.run_host()
    assert wait_up(lane.HOST), f'instance {lane.HOST} did not start'
    con = Console(lane.HOST)
    ui = Ui(con, 'host')
    for _ in range(180):
        if MENU in con.call('game.mode')['out']:
            break
        time.sleep(1)
    t_menu = time.time()
    st = ui.wait(lambda s: s['menu']['vis'], 30, 'menu item')
    check(st is not None, '"Co-op" item in the main menu')

    # C first: the menu's own intro starts ~16 s after the menu came up; click as soon as it plays
    natural = wait(lambda: intro(con), 30)
    print(f'  C: the menu intro plays {time.time() - t_menu:.1f} s after the menu: {natural}', flush=True)
    if natural:
        ui.click('menu')
        check(answers(con, 'C: click at the intro\'s first frames'), 'C: the game answers after the click')
        st = ui.wait(lambda s: s['open'] == '1', 25, 'co-op window')
        check(st is not None, 'C: the co-op window opens')
        check(not intro(con), 'C: the intro was skipped')
        ui.click('close')
        ui.wait(lambda s: s['open'] == '0', 10, 'closed')
    time.sleep(4)

    # A: Escape in the menu with no intro
    for i in range(3):
        con.call('key esc')
        check(answers(con, f'A{i + 1}: Escape, no intro'), f'A{i + 1}: the game answers after an Escape without the intro')
        time.sleep(1)
    check(MENU in con.call('game.mode')['out'], 'A: still in the galaxy menu')
    st = ui.state()
    check(st.get('open') == '0', 'A: the co-op window is not open')

    # B: the menu's own intro again: it starts each time the menu comes back (a load, then the quit to the menu); the
    # click on "Co-op" at an offset into it, or Escape at its start (the by-hand PlayCinematic intro is no model: it
    # does not end on the mod's Escape, lab 20261107-231415)
    slot, planet = fixtures.planet_of('creature').split('/')
    rounds = [('click', d) for d in (0.0, 0.3, 1.0, 3.0, 6.0, 8.5)] + [('esc', 0.0), ('esc', 9.5)]
    for kind, delay in rounds:
        name = f'B {kind} {delay:.1f} s into the intro'
        con.call(f'coop.load {slot} {planet}')
        wait(lambda: con.call('coop.avatar')['ok'], 120)
        time.sleep(3)
        con.call('coop.menu')
        wait(lambda: MENU in con.call('game.mode')['out'], 60)
        started = wait(lambda: intro(con), 30)
        time.sleep(delay)
        if kind == 'esc':
            con.call('key esc')
        else:
            ui.click('menu')
        check(answers(con, name), f'{name}: the game answers (intro playing at the start: {started})')
        if kind == 'click':
            st = ui.wait(lambda s: s['open'] == '1', 25, 'co-op window')
            check(st is not None, f'{name}: the co-op window opens')
            check(not intro(con), f'{name}: no intro plays any more')
            ui.click('close')
            ui.wait(lambda s: s['open'] == '0', 10, 'closed')
        else:
            check(wait(lambda: not intro(con), 20), f'{name}: the intro is over')
        check(answers(con, name + ', later'), f'{name}: the game still answers')
        check(MENU in con.call('game.mode')['out'], f'{name}: still in the galaxy menu')
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    check(m and m.group(1) == '0', f'no caught fault ({m.group(0) if m else "no answer"})')
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
