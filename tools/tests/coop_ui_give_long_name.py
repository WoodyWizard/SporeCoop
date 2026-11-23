#!/usr/bin/env python3
"""The give button keeps clear of the creation label with a long partner name (UIFX-8 of the external audit, BUG-1518).

BUG-1062 makes the give and jump buttons as wide as their captions, right to left, with no lower bound: a partner
named with 24 wide letters (the name limit) made "Give to <name>" start left of the end of the label "selected tribe
members: N" (and in Space a long star name pushed the jump button toward the window's left edge). Now the buttons stop
at x 400 (give) / 240 (jump) and the name in the caption is cut with "...".

Tribe fixture, two players: the guest joins as WWWWWWWWWWWWWWWWWWWWWWWW (24 x W); the host selects one of its members
(`coop.tribe select`) and opens the co-op window (F9):
  PASS: the give button shows (its caption names the partner, cut with "..."), every caption fits its button and no
  label overlaps the give button (`coop.uistate widgets`, coop_ui.layout_problems); key frame of the window.
  SPORE_FIX_OFF=core.ui_give_clamp: the give button reaches over the label's text: FAIL.
At the end the guest joins as Bob again (the lane's name).

  SPORE_LANE=2 python3 tools/tests/coop_ui_give_long_name.py
"""
import time

import lane
from coop_tribe import members
from coop_ui import VK_F9, Ui, layout_problems, widgets
from harness import waiter
from twoinst import Checks, keyframe, start, wait_guest_in

wait = waiter(step=0.5)
LONG = 'W' * 24


def join_as(guest, name):
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN.rsplit(' ', 1)[0] + ' ' + name)


def main():
    host, guest = start('tribe')
    ck = Checks()
    try:
        join_as(guest, LONG)
        ck(wait_guest_in(host, guest, 200), f'the guest is in the host\'s world as {LONG}')
        time.sleep(4)
        mine = [i for i, v in members(host).items() if v[0] == 0 and v[3] == 'player']
        if not ck(mine, f'the host owns a member to select ({len(mine)})'):
            return ck.result()
        for i in members(host):
            host.call(f'coop.tribe select {i} 0')
        host.call(f'coop.tribe select {mine[0]} 1')
        ui = Ui(host, 'host')
        st = ui.state()
        if st.get('open') != '1':
            host.call(f'key {VK_F9}')
        st = ui.wait(lambda s: s.get('open') == '1' and s['give']['vis'], 15, 'the Give button')
        ck(st is not None, f"the host's co-op window shows the Give button ({(st or {}).get('give')})")
        if st:
            cap = st['give']['text']
            print(f'  give {cap!r}', flush=True)
            print('  give row: ' + ' | '.join(w for w in host.call('coop.uistate widgets')['out'].splitlines()
                                               if '430' in w or 'Give' in w or 'selected' in w), flush=True)
            fit = [f'button {c!r} needs {need} px, has {r[2] - r[0] - 46}' for r, kind, need, c in widgets(host)
                   if kind == 'B' and 'Give' in c and need > r[2] - r[0] - 46]
            probs = fit + [x for x in layout_problems(host) if 'Give' in x]
            ck(not probs, f'the give button fits its caption and overlaps no label ({probs})')
            ck('WWW' in cap, f'the caption still names the partner ({cap!r})')
            keyframe('give_long', 'the host\'s co-op window: "selected tribe members: 1" on the left, "Give to WWW..." '
                     'on the right, nothing overlaps', players={'host': host})
            host.call(f'key {VK_F9}')
    finally:
        join_as(guest, 'Bob')
        wait_guest_in(host, guest, 200)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
