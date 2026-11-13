#!/usr/bin/env python3
"""Space, three players: "Give 10 to <player>" gives the spice to the player the button names (BUG-1050).

In Space the co-op window's "Give 10 to <player>" named coop's give target (a pick made with ">" stays), but the spice
went to the first other player of the session (spacecoop gift_target), and the ">" picker was hidden in Space.

Alice hosts, Bob and Carol are guests (tools/tests/party.py, space_free: spice in every cargo).
1. Bob opens the co-op window (F9): the Give button is shown; ">" is shown too (3 players).
2. Bob clicks ">" until the Give button names Carol (the fix off: ">" is hidden, `coop.give next` is what the button
   does, so the label still names Carol).
3. Bob clicks "Give 10 to Carol": the spice in Carol's game's cargo (`coop.space spice`, the game's inventory item
   count) grows by 10, Alice's does not change, Bob's falls by 10.
PASS with the fix; SPORE_FIX_OFF=space.give_pick: ">" hidden and the spice goes to Alice (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_give_pick.py
"""
import re
import time

from coop_ui import VK_F9, Ui
from party import Party
from twoinst import Checks, keyframe, shot


def spice(x):
    m = re.search(r'count (-?\d+)', x.call('coop.space spice'))
    return int(m.group(1)) if m else -1


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    p = Party.start('space_free', players=3)
    try:
        return run(p)
    finally:
        p.stop()


def run(p):
    ck = Checks()
    alice, bob, carol = p.byname('Alice'), p.byname('Bob'), p.byname('Carol')
    ck(len(p.together()) >= 2, f"three players in the host's Space world ({p.together()})")
    time.sleep(3)
    for x in (alice, carol):  # receivers need a cargo slot of that spice; everybody has the fixture's spice
        print(f'  {x.name} spice: {x.call("coop.space spice").strip()}', flush=True)
    s0 = {x.name: spice(x) for x in p.all}
    print(f'  spice before: {s0}', flush=True)
    ck(s0['Bob'] >= 10, f"Bob has spice to give ({s0['Bob']})")
    ui, st = p.open_window(bob)
    st = ui.wait(lambda s: s['give']['vis'], 15, 'the Give button')
    ck(st is not None, f"Bob's co-op window shows the Give button ({(st or {}).get('give')})")
    if not st:
        shot(bob.con, 'space_give_pick_nobutton.png')
        return ck.result()
    nxt = st.get('givenext', {})
    ck(nxt.get('vis'), f'">" is shown in Space with three players ({nxt})')
    print(f"  give {st['give']['text']!r}, > {nxt}", flush=True)
    for _ in range(3):
        st = ui.state()
        if 'Carol' in st['give']['text']:
            break
        if st.get('givenext', {}).get('vis'):
            ui.click('givenext')
        else:  # (the fix off: no button; what it does)
            print('  ">" hidden: coop.give next:', bob.call('coop.give next').strip(), flush=True)
        time.sleep(1)
    st = ui.state()
    ck('Carol' in st['give']['text'], f"the Give button names Carol ({st['give']['text']!r})")
    keyframe('give_pick', 'Bob\'s co-op window in Space with three players: "Give 10 to Carol" with ">" next to it, '
             'the jump button left of them, nothing overlaps', players={'host': alice.con, 'guest': bob.con})
    shot(bob.con, 'space_give_pick_window.png')
    ui.click('give')
    ok = wait(lambda: spice(carol) >= s0['Carol'] + 10, 15)
    s1 = {x.name: spice(x) for x in p.all}
    print(f'  spice after: {s1}', flush=True)
    ck(ok and s1['Carol'] == s0['Carol'] + 10, f"Carol's game has the 10 spice ({s0['Carol']} -> {s1['Carol']})")
    ck(s1['Alice'] == s0['Alice'], f"Alice's game got nothing ({s0['Alice']} -> {s1['Alice']})")
    ck(s1['Bob'] == s0['Bob'] - 10, f"Bob gave 10 ({s0['Bob']} -> {s1['Bob']})")
    bob.call(f'key {VK_F9}')
    for x in p.all:
        ck.faults(**{x.name: x.con})
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
