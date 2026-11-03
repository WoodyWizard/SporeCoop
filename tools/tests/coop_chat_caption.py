#!/usr/bin/env python3
"""A chat line can be typed in the co-op window while a caption plays (BUG-0482).

play_creature lost a chat line now and then (`the chat line takes the typing while the creature
lives`): both times the host had just found a new part and its caption ("a new part", the
cinematic) held the game's input lock (cGameInputManager +0x110). The game's UI drops every click
while the lock is held, also the click on our chat line, so the line never takes the focus and the
typing goes nowhere; core.ui_locked_clicks (BUG-0273) only presses buttons.

Each player, through real input:
1. plays CRG_ClaimNest (a caption: the input lock is held), opens the co-op window (F9 held),
   clicks the chat line, types a line and clicks Send
2. the line is in the chat line before Send and in both players' chat after it
3. the caption ends; the same without a caption (the normal path still works, typed once)

SPORE_FIX_OFF=core.ui_locked_edit: the click is dropped again, nothing is typed (the test fails).
"""
import re
import time

from coop_ui import Ui
from twoinst import Checks, start

CLAIM = '63B7060A'  # CRG_ClaimNest


def eax(con, what):
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call ' + what)['out'])
    return int(m.group(1), 16) if m else None


def cinematic(con):
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return None


def f9(con):
    con.call('key 0x78 down')  # the mod polls the key: hold it (docs/06-testing.md 6.11)
    time.sleep(0.25)
    con.call('key 0x78 up')
    time.sleep(1.0)


def centre(w):
    x1, y1, x2, y2 = w['rect']
    return (x1 + x2) // 2, (y1 + y2) // 2


def chat_once(me, other, ui, ck, what, msg):
    """open the window, click the chat line, type, Send; True when the line reached both"""
    f9(me)
    st = ui.wait(lambda s: s['open'] == '1' and s['chatedit']['vis'] and s['send']['vis'], 8, 'chat line')
    ck(st is not None, f'{what}: the co-op window opens with its chat line')
    if not st:
        return False
    x, y = centre(st['chatedit'])
    me.call(f'click {x} {y}')  # one click, as a player does (the bot of play_creature clicks once more)
    time.sleep(0.6)
    me.call(f'text "{msg}"')
    time.sleep(0.4)
    typed = ui.state()['chatedit']['text']
    print(f'  {what}: chat line holds {typed!r}, input_lock={ui.state().get("input_lock")}', flush=True)
    ck(msg in typed, f'{what}: the chat line takes the typing ({typed!r})')
    x, y = centre(st['send'])
    me.call(f'click {x} {y}')
    seen = wait(lambda: msg in me.call('coop.status')['out'] and msg in other.call('coop.status')['out'], 6)
    ck(bool(seen), f'{what}: the line is in both players\' chat')
    f9(me)
    return bool(seen)


def main():
    host, guest = start()
    ck = Checks()
    for me, other, who in ((guest, host, 'guest'), (host, guest, 'host')):
        ui = Ui(me, who)
        time.sleep(2)
        cm = eax(me, 'Simulator::cCinematicManager::Get cdecl')
        print(' ', me.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {cm:#x} s:CRG_ClaimNest 1 1 0 0 0')['out'].strip())
        ck(wait(lambda: cinematic(me) == CLAIM, 10), f'{who}: plays CRG_ClaimNest (a caption)')
        time.sleep(3)
        lock = int(ui.state().get('input_lock', '0'))
        ck(lock > 0, f'{who}: the caption holds the game\'s input lock ({lock})')
        chat_once(me, other, ui, ck, f'{who} during a caption', f'{who} caption {int(time.time()) % 1000}')
        for _ in range(3):  # the caption ends (Enter, else the game's EndCinematic)
            if not cinematic(me):
                break
            me.call('key enter')
            time.sleep(2)
        if cinematic(me):
            me.call(f'call SporeCoop::cCinematicManager_EndCinematic thiscall {cm:#x}')
            wait(lambda: not cinematic(me), 5)
        time.sleep(3)
        lock = int(ui.state().get('input_lock', '0'))
        ck(lock == 0, f'{who}: the input lock is free after the caption ({lock})')
        msg = f'{who} free {int(time.time()) % 1000}'
        chat_once(me, other, ui, ck, f'{who} without a caption', msg)
        # typed once: a key the game delivered itself is never added a second time by the mod
        line = [c for c in ui.state()['chat'] if msg in c]
        ck(line and line[-1].count(msg) == 1, f'{who}: the line was typed once ({line[-1:]!r})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
