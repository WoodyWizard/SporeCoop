#!/usr/bin/env python3
"""Cell stage: the cell's own effects show on the partner's cell (BUG-0451, docs/re/creature.md#creature-effects-relay).

A cell makes its effects (a hit's particles, a death, a hatch) in the game's effect world at the
cell's position. The mod's world-effect relay sends them when the effect is on the local player's
own cell and the partner plays the same effect at the same world position (where its mirror of that
cell is). `coop.effects` prints the partner's own game effect object (vt 0x10 active) and its
position, compared with the host cell puppet's position from `coop.status`.

  coop_cell_effects.py   (SPORE_FIX_OFF=creature.effects_relay must FAIL)
"""
import math
import re
import time

from twoinst import Checks, start


def busy(con):
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def free_games(host, guest, seconds=25):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if busy(host) == '-' and busy(guest) == '-':
            return True
        for c in (host, guest):
            c.call('key enter')
            c.call('click 1418 1044')
        time.sleep(1)
    return busy(host) == '-' and busy(guest) == '-'


def nums(con):
    m = re.search(r'effects: fix=(\d) sent=(\d+) received=(\d+) played=(\d+) stopped=(\d+) skipped=(\d+) '
                  r'failed=(\d+) dropped_rate=(\d+) world=(\d+)', con.call('coop.effects')['out'])
    if not m:
        return {}
    keys = ('fix', 'sent', 'received', 'played', 'stopped', 'skipped', 'failed', 'dropped', 'world')
    return {k: int(v) for k, v in zip(keys, m.groups())}


def last_rx(con, who):
    out = con.call('coop.effects')['out']
    m = re.search(r'effects received last from (\d+): id=([0-9A-F]{8}) key=([0-9A-F]{8}) pool=(\d+) stop=(\d+) '
                  r'game=(\d) active=(\d)', out)
    if not m or int(m.group(1)) != who:
        return None
    p = re.search(r'effects received last at: ([-\d.]+) ([-\d.]+) ([-\d.]+)', out)
    return {'id': m.group(2), 'pool': int(m.group(4)), 'stop': int(m.group(5)), 'game': int(m.group(6)),
            'active': int(m.group(7)), 'pos': tuple(float(x) for x in p.groups()) if p else None}


def own_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(x) for x in m.groups()) if m else None


def puppet_pos(con, who):
    m = re.search(r"remote %d '.*?' puppet=\S+ state=\S+ age=[\d.]+s pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)" % who,
                  con.call('coop.status')['out'])
    return tuple(float(x) for x in m.groups()) if m else None


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))) if a and b else None


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'the guest is in the host\'s Cell stage')
    ck(free_games(host, guest), f'both games take input (busy: {busy(host)} / {busy(guest)})')
    gid = 1
    before = nums(host).get('received', 0)
    gpos = puppet_pos(host, gid)
    print(f'  guest cell on the host: {gpos}', flush=True)
    for attempt in range(3):
        out = host.call(f'coop.pool hurt {gid} 999')['out'].strip()  # the host damages the guest's cell
        print(f'  guest cell hurt: {out}', flush=True)
        t0 = time.time()
        while time.time() - t0 < 4:
            time.sleep(0.15)
            rx = last_rx(host, gid)
            if nums(host).get('received', 0) > before and rx and rx['pool'] == 5 and rx['game'] == 1 and rx['active'] == 1:
                d = dist(rx['pos'], own_pos(guest))
                p = dist(rx['pos'], gpos)
                print(f'  received {rx}, {d if d is None else round(d, 2)} m from the guest cell at send time, '
                      f'{p if p is None else round(p, 2)} m from the puppet before it', flush=True)
                ck(d is not None and d < 3.0 and p is not None and p < 5.0,
                   f'the host\'s game creates the guest cell\'s effect at its cell ({d and round(d, 2)} m)')
                ck.faults(host=host, guest=guest)
                raise SystemExit(ck.result())
        if busy(host) != '-' or busy(guest) != '-':
            free_games(host, guest, 8)
    ck(False, 'the host received a world effect of the guest\'s cell')
    ck.faults(host=host, guest=guest)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
