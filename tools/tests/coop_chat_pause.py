#!/usr/bin/env python3
"""A chat line typed while the host's "game saved" box is up keeps its typing (BUG-0551).

play_creature (0.7.6 final check, out/lab/20261104-135838) lost a host's chat line typed while the host was
paused saving the world for a guest coming back: the box is a modal window, so the mod takes the typing
(core.ui_locked_edit / core.ui_modal_edit, BUG-0482), but
 a) the mod's own click on the box's OK button (world.cpp confirms the box) ended the mod's typing like a
    click of the player elsewhere: what the player typed after it went nowhere;
 b) a frame held by the save took the click on the line and the keys after it at once, and the keys of the
    frame of a click were dropped.

The host, through real input (the window's own chat line text is checked, then both players' chat):
 1. `coop.save hold` (the box stays up), F9, a click on the chat line, the first half typed; the box is
    confirmed by the mod (`coop.save confirm`: its click on OK), the second half typed: the line holds both
    halves; Send: the whole line is in both players' chat.
 2. the box again; the main thread is held (`kit.test.hang`), during the hold a click on the chat line and a
    line typed (the queue keeps both, the next frame takes them at once): the line holds it.

SPORE_FIX_OFF=core.ui_locked_edit_keep: the second half (1) and the whole line (2) are lost: FAILS.
"""
import re
import time

from coop_chat_caption import centre, f9, wait
from coop_ui import Ui
from twoinst import Checks, shot, start


def box(con):
    m = re.search(r'saved_box=(\d)', con.call('coop.save box')['out'])
    return bool(m and m.group(1) == '1')


def box_up(host, ui, ck, what):
    print(' ', host.call('coop.save hold')['out'].strip(), flush=True)
    ok = wait(lambda: box(host) and ui.state().get('modal') == '1', 20)
    ck(ok, f'{what}: the "game saved" box is up and is the game\'s modal window')
    return ok


def confirm(host, ui, ck, what):
    print(' ', host.call('coop.save confirm')['out'].strip(), flush=True)
    ck(wait(lambda: not box(host) and ui.state().get('modal') == '0', 15), f'{what}: the mod confirmed the box')
    time.sleep(1)


def send(host, guest, ui, ck, what, msg):
    st = ui.state()
    x, y = centre(st['send'])
    host.call(f'click {x} {y}')
    seen = wait(lambda: msg in host.call('coop.status')['out'] and msg in guest.call('coop.status')['out'], 8)
    ck(bool(seen), f'{what}: the whole line is in both players\' chat')


def main():
    host, guest = start('creature')
    ck = Checks()
    ui = Ui(host, 'host')
    time.sleep(2)
    tag = int(time.time()) % 1000

    # ---- 1. the mod's own click on the box's OK while the player types ----
    what = 'over the box confirm'
    if box_up(host, ui, ck, what):
        f9(host)
        st = ui.wait(lambda s: s['open'] == '1' and s['chatedit']['vis'], 8, 'chat line')
        ck(st is not None, f'{what}: the co-op window is open with its chat line')
        if st:
            x, y = centre(st['chatedit'])
            host.call(f'click {x} {y}')
            time.sleep(0.6)
            first, second = f'host pause {tag}', ' and after'
            host.call(f'text "{first}"')
            time.sleep(0.6)
            typed = ui.state()['chatedit']['text']
            print(f'  {what}: before the confirm the line holds {typed!r}', flush=True)
            ck(first in typed, f'{what}: the line takes the typing under the box ({typed!r})')
            shot(host, 'chat_pause_box.png')
            confirm(host, ui, ck, what)
            host.call(f'text "{second}"')
            time.sleep(0.8)
            typed = ui.state()['chatedit']['text']
            print(f'  {what}: after the confirm the line holds {typed!r}', flush=True)
            ck(typed == first + second, f'{what}: the typing goes on after the mod\'s click on OK ({typed!r})')
            send(host, guest, ui, ck, what, first + second)
    if box(host):
        confirm(host, ui, ck, what)

    # ---- 2. a frame held by the save: the click and the typing come in one frame ----
    what = 'in a held frame'
    if box_up(host, ui, ck, what):
        st = ui.state()
        if st.get('open') != '1':
            f9(host)
            st = ui.wait(lambda s: s['open'] == '1' and s['chatedit']['vis'], 8, 'chat line')
        if st and st['chatedit']['vis']:
            if st['chatedit']['text']:
                ck(False, f'{what}: the chat line is empty before ({st["chatedit"]["text"]!r})')
            x, y = centre(st['chatedit'])
            msg = f'host held {tag}'
            print(' ', host.call('kit.test.hang 2500')['out'].strip(), flush=True)
            time.sleep(0.5)  # the main thread sleeps: the messages below wait in its queue
            host.call(f'button {x} {y} down')
            host.call(f'button {x} {y} up')
            host.call(f'text "{msg}"')
            ok = wait(lambda: host.call('frame')['ok'] and msg in ui.state()['chatedit']['text'], 8)
            typed = ui.state()['chatedit']['text']
            print(f'  {what}: the line holds {typed!r}', flush=True)
            ck(ok, f'{what}: the keys after the click in a held frame go into the line ({typed!r})')
            confirm(host, ui, ck, what)
            if ok:
                send(host, guest, ui, ck, what, msg)
        else:
            ck(False, f'{what}: the co-op window is open with its chat line')
    if box(host):
        confirm(host, ui, ck, what)
    if ui.state().get('open') == '1':
        f9(host)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
