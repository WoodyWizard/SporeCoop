#!/usr/bin/env python3
"""Three players: a relayed world effect reaches the second guest (BUG-0451 review item 1).

The host relays a guest's MSG_EFFECT to the other guests. The relay used to drop the 0x44-byte
transform of a pool 5 (world) effect, so a guest 2 dropped every world effect of a guest 1 (and of
the host): the message came without enough bytes. Here both guests are in the creature fixture:
each machine creates a steady world effect at its own creature (`coop.effects load`, the game's
effect manager), and the other guest's game must create the game's effect object (active, at the
actor's world position) from the relayed message.

  coop_effects_party.py [--fixture cell|creature|tribe|civ|space]   (SPORE_FIX_OFF=creature.effects_relay must FAIL)

Every stage (BUG-0471): the same three cases in the stage's fixture, the effect id is the last one the
stage's game created (`coop.effects load ... 0`; Creature keeps the mating call E1B24DDB), the actor's
position is where its own game has its player (coop.sync), and each game's relay costs under 1 % of
its frame time while the load runs (hook work + effects::tick, from coop.effects).
"""
import argparse
import math
import re
import time

from party import Party
from twoinst import Checks


def busy(con):
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def free_games(p, seconds=30):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if all(busy(x.con) == '-' for x in p.all):
            return True
        for x in p.all:
            x.call('key enter')
            x.call('click 1418 1044')
        time.sleep(1)
    return all(busy(x.con) == '-' for x in p.all)


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
        m = re.search(r'effects received last from (\d+): id=([0-9A-F]{8}) key=([0-9A-F]{8}) pool=(\d+) stop=(\d+) '
                      r'game=(\d) active=(\d)', out)
        if not m or int(m.group(1)) != who:
            return None
        p = re.search(r'effects received last at: ([-\d.]+) ([-\d.]+) ([-\d.]+)', out)
        return {'id': m.group(2), 'pool': int(m.group(4)), 'stop': int(m.group(5)), 'game': int(m.group(6)),
                'active': int(m.group(7)), 'pos': tuple(float(x) for x in p.groups()) if p else None}
    p = re.search(r'effects received start at: ([-\d.]+) ([-\d.]+) ([-\d.]+)', out)
    return {'id': m.group(2), 'pool': int(m.group(4)), 'stop': 0, 'game': int(m.group(5)),
            'active': int(m.group(6)), 'pos': tuple(float(x) for x in p.groups()) if p else None}


def pos_of(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(x) for x in m.groups()) if m else None


def cost_raw(con):
    """(hook work ms, effects::tick ms, wall s) since the session start, from coop.effects"""
    out = con.call('coop.effects')['out']
    t = re.search(r'effects cost: tick avg ([\d.]+) max [\d.]+ ms/frame over (\d+) frames', out)
    h = re.search(r'effects hook work: ([\d.]+) ms over ([\d.]+) s', out)
    if not t or not h:
        return None
    return float(h.group(1)), float(t.group(1)) * int(t.group(2)), float(h.group(2))


def cost(a, b):
    """the relay's share of the main thread between two cost_raw reads, in percent"""
    if not a or not b or b[2] <= a[2]:
        return None
    return ((b[0] - a[0]) + (b[1] - a[1])) / ((b[2] - a[2]) * 1000) * 100


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))) if a and b else None


FX_ID = 'E1B24DDB'
OWN_POS = None


def case(ck, actor, receiver, actor_id, what, wait=12.0):
    """the actor makes world effects, `receiver`'s game must create them at the actor's position"""
    at = OWN_POS(actor) if OWN_POS else pos_of(actor.con)
    before = nums(receiver.con).get('received', 0)
    actor.call(f'coop.effects load 1 400 {FX_ID}')
    t0 = time.time()
    try:
        while time.time() - t0 < wait:
            time.sleep(0.3)
            rx = last_rx(receiver.con, actor_id)
            if nums(receiver.con).get('received', 0) > before and rx and rx['pool'] == 5 and rx['game'] == 1 \
                    and rx['active'] == 1:
                d = dist(rx['pos'], at)
                print(f'  {what}: received {rx}, {d if d is None else round(d, 2)} m from the actor', flush=True)
                ck(d is not None and d < 8.0,
                   f'{what}: the second guest\'s game creates the world effect at the actor ({d and round(d, 2)} m)')
                return
            if busy(actor.con) != '-' or busy(receiver.con) != '-':
                free_games_local(actor, receiver, 8)
    finally:
        actor.call('coop.effects load 0')
    ck(False, f'{what}: the second guest\'s game creates the world effect')


def free_games_local(a, b, seconds):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if busy(a.con) == '-' and busy(b.con) == '-':
            return
        for x in (a, b):
            x.call('key enter')
            x.call('click 1418 1044')
        time.sleep(1)


def main():
    global FX_ID, OWN_POS
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature', choices=('cell', 'creature', 'tribe', 'civ', 'space'))
    a = ap.parse_args()
    p = Party.start(a.fixture, players=3, join_together=True)
    ck = Checks()
    print(f'  fixture {a.fixture}', flush=True)
    if a.fixture in ('tribe', 'civ', 'space'):  # the relay is off there in 0.7.4 (coop.fix effects.other_stages)
        for x in p.all:
            x.con.call('coop.fix effects.other_stages on')
    if a.fixture != 'creature':
        FX_ID = '0'  # the last effect id this stage's game created
        OWN_POS = p.own_pos
    ids = p.ids()
    host, bob, carol = p.byname('Alice'), p.byname('Bob'), p.byname('Carol')
    print(f'  ids {ids}', flush=True)
    ck(free_games(p), 'all three games take input')
    ck(all(p.is_in(g) for g in p.guests), f'both guests play the host\'s {a.fixture} stage')
    time.sleep(3)
    # the host's world effect must reach guest 2 (and guest 1), then guest 1's must reach guest 2
    case(ck, host, carol, host.pid, 'host world effect on guest 2')
    case(ck, host, bob, host.pid, 'host world effect on guest 1')
    case(ck, bob, carol, bob.pid, 'guest 1 world effect on guest 2')
    # the frame-time cost of the relay with every machine sending at once (each at 2.5 effects/s)
    before = {x.name: cost_raw(x.con) for x in p.all}
    for x in p.all:
        x.call(f'coop.effects load 1 400 {FX_ID}')
    time.sleep(20)
    for x in p.all:
        c = cost(before[x.name], cost_raw(x.con))
        x.call('coop.effects load 0')
        print(f'  {x.name}: relay cost {c if c is None else round(c, 4)} % of the frame time', flush=True)
        ck(c is not None and c < 1.0, f'{x.name}: the relay costs under 1 % of the frame time ({c and round(c, 4)} %)')
    ck.faults(host=host.con, guest=bob.con, extra=carol.con)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
