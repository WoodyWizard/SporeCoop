#!/usr/bin/env python3
"""A chat line can be typed in the co-op window while a modal box of the game is up (BUG-0482 follow-up).

play_creature (rel/0.7.4, out/lab/20261103-224540) lost a chat line typed while the host's "game saved" box was
up (the host saved the world for a guest coming back): the box is the window manager's modal window and takes
every click, like the input lock of a caption, so the chat line never took the focus. core.ui_locked_edit
covered the input lock only.

The host, through real input: saves (`coop.save hold`: the mod does not confirm the box), the box is up and is
the window manager's modal window, opens the co-op window (F9 held), clicks the chat line, types a line, clicks
Send: the line is in the chat line and in both players' chat. Then the box is confirmed and the same without it
(the normal path still works, typed once).
SPORE_FIX_OFF=core.ui_modal_edit: the click is dropped, nothing is typed (the test fails).
"""
import re
import time

from coop_chat_caption import chat_once, wait
from coop_ui import Ui
from twoinst import Checks, shot, start


def box(con):
    m = re.search(r'saved_box=(\d)', con.call('coop.save box')['out'])
    return m and m.group(1) == '1'


def main():
    host, guest = start('creature')
    ck = Checks()
    ui = Ui(host, 'host')
    time.sleep(2)
    print(' ', host.call('coop.save hold')['out'].strip(), flush=True)
    ck(wait(lambda: box(host), 15), "host: the game's \"game saved\" box is up")
    time.sleep(1)
    st = ui.state()
    print(f"  host: input_lock={st.get('input_lock')} modal={st.get('modal')}", flush=True)
    ck(st.get('modal') == '1', "host: the box is the game's modal window")
    shot(host, 'chat_modal_box.png')
    chat_once(host, guest, ui, ck, 'host under the "game saved" box', f'host box {int(time.time()) % 1000}')
    print(' ', host.call('coop.save confirm')['out'].strip(), flush=True)
    ck(wait(lambda: not box(host) and ui.state().get('modal') == '0', 10), 'host: the box is confirmed and gone')
    time.sleep(2)
    msg = f'host free {int(time.time()) % 1000}'
    chat_once(host, guest, ui, ck, 'host without a box', msg)
    line = [c for c in ui.state()['chat'] if msg in c]
    ck(line and line[-1].count(msg) == 1, f'host: the line was typed once ({line[-1:]!r})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
