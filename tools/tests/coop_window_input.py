#!/usr/bin/env python3
"""BUG-0713 (class BUG-0429: mod input read globally): a key pressed while another program has the focus does
nothing in the game; the same key on the game window still works.

The mod polled the physical keyboard (GetAsyncKeyState): F9 typed in a browser or a chat program toggled the co-op
window, and the social digits and the Civ marker read the keyboard and the screen cursor wherever they were. All mod
input now comes from the game window's own messages (mouse.h key_presses / last_move).

On both games of a Creature session (the guest's game too: the rule is per machine):
  1. F9 "elsewhere" (`key f9 elsewhere`: the physical key is down, the game window gets no message): the co-op
     window stays closed, 3 times
  2. F9 on the game window: the window opens; again: it closes (the key itself is not lost)

  SPORE_FIX_OFF=input.window coop_window_input.py   (step 1 FAILS: F9 in another program opens the window)
"""
import time

from coop_ui import Ui
from twoinst import Checks, start


def is_open(ui):
    st = ui.state()
    return st.get('open') == '1'


def main():
    host, guest = start('creature')
    ck = Checks()
    for con, who in ((host, 'host'), (guest, 'guest')):
        ui = Ui(con, who)
        if is_open(ui):
            con.call('key f9')
            time.sleep(1.5)
        ck(not is_open(ui), f'{who}: the co-op window is closed at the start')
        opened = 0
        for _ in range(3):
            con.call('key f9 elsewhere')
            time.sleep(1.5)
            if is_open(ui):
                opened += 1
                con.call('key f9')  # close it again for the next try
                time.sleep(1.5)
        print(f'  {who}: F9 pressed in another program 3 times: the window opened {opened} times', flush=True)
        ck(opened == 0, f'{who}: F9 in another program does not open the co-op window ({opened}/3)')
        con.call('key f9')
        ok_open = ui.wait(lambda s: s.get('open') == '1', 10, 'the window open') is not None
        ck(ok_open, f'{who}: F9 on the game window opens the co-op window')
        con.call('key f9')
        ok_closed = ui.wait(lambda s: s.get('open') == '0', 10, 'the window closed') is not None
        ck(ok_closed, f'{who}: F9 on the game window closes it again')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
