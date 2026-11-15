#!/usr/bin/env python3
"""Galaxy menu: Escape and "Co-op" clicks around the menu intro keep the game answering (BUG-0184).

Once in 15 runs of coop_ui.py (2026-10-24) the host's console timed out right after its click on "Co-op" during the
menu intro: the mod posts WM_KEYDOWN/UP Escape to skip the intro (coop_ui.cpp, docs/re/space.md 7.9), the game
handles it a frame or more later. Candidate (BUG-0184, 2026-10-30): an Escape that reaches the galaxy menu after the
intro (or before it starts) opens something modal that stops the frames. One game, alone, in the galaxy menu:

  C  six starts of the game, each with the menu's own intro (it comes once per start, ~16 s after the menu came up):
     "Co-op" clicked 0 / 0.3 / 1 / 3 / 7 s into it (the mod's Escape skip, also near the intro's end), or Escape at
     its start: the frames go on, the co-op window opens after a click, the intro is over
  A  then Escape with no intro playing, 3 times: frames go on, the console answers, no window stays up
The intro started by hand (PlayCinematic) does not end on the mod's Escape, and a return to the menu (coop.load, then
coop.menu) does not start it again (lab 20261107-231415, 20261107-233528): neither is a model of the player's intro.

  SPORE_LANE=9 python3 tools/tests/coop_menu_escape.py
"""
import re
import sys
import time

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


def cold_menu():
    """the game started anew (the menu's own intro comes once per start, ~16 s after the menu came up)"""
    lane.sh('kill')
    time.sleep(2)
    lane.run_host()
    assert wait_up(lane.HOST), f'instance {lane.HOST} did not start'
    con = Console(lane.HOST)
    for _ in range(180):
        if MENU in con.call('game.mode')['out']:
            break
        time.sleep(1)
    return con, time.time()


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
    # C rounds: one start each; "Co-op" clicked this many s into the menu's own intro (the mod's Escape skip, also
    # near the intro's end), or Escape (the player's own skip key); then A once: Escape with no intro
    rounds = [('click', 0.0), ('click', 0.3), ('click', 1.0), ('click', 3.0), ('click', 7.0), ('esc', 0.0)]
    faults_total = 0
    for n, (kind, delay) in enumerate(rounds):
        name = f'C{n + 1} {kind} {delay:.1f} s into the intro'
        con, t_menu = cold_menu()
        ui = Ui(con, 'host')
        st = ui.wait(lambda s: s['menu']['vis'], 30, 'menu item')
        check(st is not None, f'{name}: "Co-op" item in the main menu')
        natural = wait(lambda: intro(con), 30)
        print(f'  {name}: the menu intro plays {time.time() - t_menu:.1f} s after the menu: {natural}', flush=True)
        check(natural, f'{name}: the menu\'s own intro plays')
        if not natural:
            continue
        time.sleep(delay)
        if kind == 'esc':
            con.call('key esc')
        else:
            ui.click('menu')
        check(answers(con, name), f'{name}: the game answers')
        if kind == 'click':
            st = ui.wait(lambda s: s['open'] == '1', 25, 'co-op window')
            check(st is not None, f'{name}: the co-op window opens')
            ui.click('close')
            ui.wait(lambda s: s['open'] == '0', 10, 'closed')
        check(wait(lambda: not intro(con), 20), f'{name}: the intro is over')
        check(answers(con, name + ', later'), f'{name}: the game still answers')
        check(MENU in con.call('game.mode')['out'], f'{name}: still in the galaxy menu')
        if n == len(rounds) - 1:
            time.sleep(4)
            for i in range(3):  # A: Escape in the menu with no intro (the candidate of 2026-10-30)
                con.call('key esc')
                check(answers(con, f'A{i + 1}: Escape, no intro'), f'A{i + 1}: the game answers after an Escape without the intro')
                time.sleep(1)
            check(MENU in con.call('game.mode')['out'], 'A: still in the galaxy menu')
            check(ui.state().get('open') == '0', 'A: the co-op window is not open')
        m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
        faults_total += int(m.group(1)) if m else 1
    check(faults_total == 0, f'no caught fault in any start ({faults_total})')
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
