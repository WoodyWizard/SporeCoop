#!/usr/bin/env python3
"""Tribe: the co-op window's buttons work while a modal box of the game is up and the input lock is 0 (BUG-0858).

The fork PandizeLive/SporeCoop-Lineages (based on our 0.7.3.6) saw the co-op window (F9) open in Tribe with the
game's input lock at 0 while no button took its click: a native modal (a registration dialog, an online-service
error box) took them. 0.7.3.6 forwarded a swallowed click only while the input lock was held (BUG-0273); since 0.7.4
a modal window of the game (cWindowManager +0x82C, BUG-0482 `core.ui_modal_edit`) counts as well.

1. Two games on the tribe fixture. The host saves with the box held (`coop.save hold`: the "game saved" box stays up,
   the window manager's modal window); the game's input lock is 0.
2. The host opens the co-op window (F9 held) and clicks its "Settings" button (a real click).
3. PASS: the window shows the settings page within 3 s (the button's own action), then "Back" brings the lobby back;
   the box is confirmed and the same click works without it.
SPORE_FIX_OFF=core.ui_modal_edit: the click is dropped, the page stays the lobby (FAIL).
"""
import re
import time

from coop_chat_caption import f9, wait
from coop_ui import Ui
from twoinst import Checks, shot, start


def box(con):
    m = re.search(r'saved_box=(\d)', con.call('coop.save box')['out'])
    return m and m.group(1) == '1'


def press(con, ui, name, page, what, ck):
    st = ui.wait(lambda s: s['open'] == '1' and s[name]['vis'], 8, name)
    if not ck(st is not None, f'{what}: the co-op window is open with its {name} button'):
        return False
    x1, y1, x2, y2 = st[name]['rect']
    con.call(f'move {(x1 + x2) // 2} {(y1 + y2) // 2}')
    time.sleep(0.3)
    con.call(f'click {(x1 + x2) // 2} {(y1 + y2) // 2}')
    got = ui.wait(lambda s: s['page'] == page, 3, f'page {page}')
    print(f'  {what}: {name} -> page {ui.state().get("page")} (input_lock={ui.state().get("input_lock")} '
          f'modal={ui.state().get("modal")})', flush=True)
    return ck(got is not None, f'{what}: the click on {name} did its action (page {page})')


def main():
    host, guest = start('tribe')
    ck = Checks()
    ui = Ui(host, 'host')
    time.sleep(3)
    print(' ', host.call('coop.save hold')['out'].strip(), flush=True)
    ck(wait(lambda: box(host), 15), "host: the game's \"game saved\" box is up")
    time.sleep(1)
    st = ui.state()
    print(f"  host: input_lock={st.get('input_lock')} modal={st.get('modal')} mode={host.call('game.mode')['out'].strip()}", flush=True)
    ck(st.get('modal') == '1' and st.get('input_lock') == '0', "host: a modal box of the game is up, the input lock is 0")
    f9(host)
    shot(host, 'ui_modal_tribe_box.png')
    if press(host, ui, 'settings', 'settings', 'under the box', ck):
        press(host, ui, 'setback', 'lobby', 'under the box, back', ck)
    f9(host)
    print(' ', host.call('coop.save confirm')['out'].strip(), flush=True)
    ck(wait(lambda: not box(host) and ui.state().get('modal') == '0', 10), 'host: the box is confirmed and gone')
    time.sleep(2)
    f9(host)
    if press(host, ui, 'settings', 'settings', 'without a box', ck):
        press(host, ui, 'setback', 'lobby', 'without a box, back', ck)
    f9(host)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
