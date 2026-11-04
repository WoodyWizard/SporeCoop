#!/usr/bin/env python3
"""Tribe stage: an effect of a player's tribe member shows on the partner's mirror (BUG-0451).

A player tribe member is a cCreatureBase: its effects are made in the game's effect world at the
member. The world-effect relay sends the ones on the local player's own members; the partner's game
creates the same effect at the same world position, where its mirror of that member stands. Checked
in the partner's game: `coop.effects` (the game effect object's vt 0x10 and position) against the
member's position before the hit and the mirror's own position.

  coop_tribe_effects.py   (SPORE_FIX_OFF=creature.effects_relay must FAIL)
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
    m = re.search(r'effects received start from (\d+): id=([0-9A-F]{8}) key=([0-9A-F]{8}) pool=(\d+) '
                  r'game=(\d) active=(\d)', out)
    if not m or int(m.group(1)) != who:
        return None
    p = re.search(r'effects received start at: ([-\d.]+) ([-\d.]+) ([-\d.]+)', out)
    return {'id': m.group(2), 'pool': int(m.group(4)), 'game': int(m.group(5)), 'active': int(m.group(6)),
            'pos': tuple(float(x) for x in p.groups()) if p else None}


def members(con):
    """{id: (owner, x, y, z)} from coop.tribe's member lines"""
    out = {}
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s*#(\d+) \S+ owner=(\d+).*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            out[m.group(1)] = (int(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(5)))
    return out


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))) if a and b else None


def main():
    host, guest = start('tribe')
    for c in (host, guest):  # the relay is off in Tribe in 0.7.4 (coop.fix effects.other_stages)
        c.call('coop.fix effects.other_stages on')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'the guest is in the host\'s Tribe stage')
    ck(free_games(host, guest), f'both games take input (busy: {busy(host)} / {busy(guest)})')
    hm, gm = members(host), members(guest)
    mine = [i for i, v in hm.items() if v[0] == 0 and i in gm]  # host-owned members the guest mirrors
    ck(bool(mine), f'the player tribe is split and mirrored ({len(mine)} host members)')
    if not mine:
        ck.faults(host=host, guest=guest)
        raise SystemExit(ck.result())
    mid = mine[0]
    mpos, gpos = hm[mid][1:], gm[mid][1:]
    before = nums(guest).get('received', 0)
    print(f'  member #{mid}: host {mpos}, guest mirror {gpos}', flush=True)
    for attempt in range(3):
        out = host.call(f'coop.tribe pincer {mid} {mid} 100000')['out'].strip()  # the game's damage path
        print(f'  pincer #{mid}: {out}', flush=True)
        t0 = time.time()
        while time.time() - t0 < 5:
            time.sleep(0.15)
            rx = last_rx(guest, 0)
            if nums(guest).get('received', 0) > before and rx and rx['pool'] in (3, 5) and rx['game'] == 1 \
                    and rx['active'] == 1:
                d = dist(rx['pos'], mpos)
                p = dist(rx['pos'], gpos)
                print(f'  received {rx}, {d if d is None else round(d, 2)} m from the member, '
                      f'{p if p is None else round(p, 2)} m from its mirror', flush=True)
                ck(d is not None and d < 4.0 and p is not None and p < 5.0,
                   f'the guest\'s game creates the member\'s effect at its mirror ({d and round(d, 2)} m)')
                ck.faults(host=host, guest=guest)
                raise SystemExit(ck.result())
        if busy(host) != '-' or busy(guest) != '-':
            free_games(host, guest, 8)
    ck(False, 'the guest received a world effect of the host\'s member')
    ck.faults(host=host, guest=guest)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
