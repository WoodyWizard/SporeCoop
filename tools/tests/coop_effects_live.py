#!/usr/bin/env python3
"""BUG-0451 review item 4: the world effects the relay created do not pile up.

A relayed pool 5 effect is created on the receiver and nothing stops it: an effect that loops (the
mating-call hearts id does when the effect world owns it) would stay forever. The receiver keeps a
bounded list: the oldest is stopped when more than 64 are live, any still active after 8 s is
stopped, and the list is dropped on a stage / world change. Here the host creates 20 world effects
a second for 30 s; the guest's live relayed count must stay at or under 64 (it would be ~160 with
the 8 s life alone and ~600 without any stop), and after the load stops it must return to 0 (the
8 s stop is what ends the last ones).

  coop_effects_live.py   (SPORE_FIX_OFF=creature.effects_relay fails: nothing is relayed at all)
"""
import re
import time

from twoinst import Checks, start


def busy(con):
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def free_games(h, g, seconds=30):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if busy(h) == '-' and busy(g) == '-':
            return True
        for c in (h, g):
            c.call('key enter')
            c.call('click 1418 1044')
        time.sleep(1)
    return busy(h) == '-' and busy(g) == '-'


def live(con):
    m = re.search(r'effects world live: live=(\d+) stopped=(\d+) expired=(\d+)', con.call('coop.effects')['out'])
    return tuple(int(x) for x in m.groups()) if m else None


def sent(con):
    m = re.search(r'effects: fix=\d sent=(\d+)', con.call('coop.effects')['out'])
    return int(m.group(1)) if m else 0


def main():
    h, g = start('creature')
    ck = Checks()
    ck(g.call('coop.avatar')['ok'], 'the guest is in the host\'s world')
    ck(free_games(h, g), f'both games take input (busy: {busy(h)} / {busy(g)})')
    ck(live(g) is not None, f'coop.effects has the live counter ({live(g)})')
    s0 = sent(h)
    h.call('coop.effects load 20 1000 E1B24DDB')  # under the 40/s relay limit; the id stays active
    top = 0
    t0 = time.time()
    while time.time() - t0 < 30:
        time.sleep(0.5)
        n = live(g)
        if n:
            top = max(top, n[0])
            ck(n[0] <= 64, f'the live relayed world effects stay bounded ({n[0]} <= 64)')
    h.call('coop.effects load 0')
    print(f'  live max {top}, sent {sent(h) - s0}, at stop {live(g)}', flush=True)
    ck(top >= 60, f'the cap was exercised (max {top} live)')
    ck(sent(h) - s0 > 400, f'the effects were relayed ({sent(h) - s0} sent)')
    t0 = time.time()
    while time.time() - t0 < 14:
        time.sleep(0.5)
        if live(g)[0] == 0:
            break
    n = live(g)
    print(f'  after the load stops: {n}', flush=True)
    ck(n is not None and n[0] == 0, f'the live relayed effects return to 0 ({n})')
    ck(n is not None and n[1] > 0 and n[2] > 0, f'effects were stopped after 8 s and at the cap ({n})')
    ck.faults(host=h, guest=g)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
